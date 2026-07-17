#!/usr/bin/env python3
"""gen-unicode-tables.py — regenerate src/unicode_tables.h and
src/unicode_tables.c from the raw Unicode Character Database.

Usage:
    python3 tools/gen-unicode-tables.py [--version 16.0.0]

Downloads (and caches locally in .unicode-cache/, gitignored — not
committed, matching the pattern Chez Scheme and Kaappi both use of
regenerating from the authoritative UCD source rather than shipping the
~3.5MB of raw source files in the repo) four files from unicode.org:

  - UnicodeData.txt          general category, simple upper/lower mapping
  - DerivedCoreProperties.txt Alphabetic, Uppercase, Lowercase properties
  - PropList.txt              White_Space property
  - CaseFolding.txt            simple case-folding mappings

Sourcing directly from these files (rather than Python's bundled
`unicodedata` module) decouples the generated tables' Unicode version from
whatever Python happens to be installed when someone regenerates them, and
uses the official derived properties (Alphabetic/Uppercase/Lowercase/
White_Space) rather than approximating them from general category alone —
matching how both Chez Scheme (unicode/extract-info.ss) and Kaappi
(tools/gen_unicode_tables.py) source their own tables.

Tables emitted:
  - Five classification range tables (sorted, non-overlapping
    [start, end] pairs, inclusive): alphabetic, numeric (decimal digit),
    whitespace, uppercase, lowercase.
  - Three simple case-mapping tables (codepoint -> codepoint, 1:1 only —
    multi-character mappings like German ss->"SS" are out of scope for a
    single-character result and are skipped, left as identity by the
    lookup layer): upper mapping, lower mapping, fold mapping (from
    CaseFolding.txt's common 'C' + simple 'S' status entries — distinct
    from the lowercase map for codepoints that fold differently than they
    lowercase, e.g. U+212A KELVIN SIGN folds to 'k' but has no simple
    lowercase mapping of its own).

See src/unicode.c for the lookup layer that consumes these tables.
"""

import argparse
import os
import sys
import urllib.request

UCD_BASE = "https://www.unicode.org/Public/{version}/ucd/"
CACHE_DIR = ".unicode-cache"
FILES = ["UnicodeData.txt", "DerivedCoreProperties.txt", "PropList.txt", "CaseFolding.txt"]


def fetch(version):
    os.makedirs(CACHE_DIR, exist_ok=True)
    paths = {}
    for name in FILES:
        path = os.path.join(CACHE_DIR, f"{version}-{name}")
        if not os.path.exists(path):
            url = UCD_BASE.format(version=version) + name
            print(f"Downloading {url}...")
            urllib.request.urlretrieve(url, path)
        paths[name] = path
    return paths


def merge_ranges(ranges):
    ranges = sorted(ranges)
    merged = []
    for lo, hi in ranges:
        if merged and lo <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
        else:
            merged.append((lo, hi))
    return merged


def parse_prop_ranges(path, wanted_props):
    """Parse a PropList.txt/DerivedCoreProperties.txt-style file; returns
    {prop_name: [(lo, hi), ...]} for each name in wanted_props."""
    result = {p: [] for p in wanted_props}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = line.split(";")
            if len(fields) < 2:
                continue
            prop = fields[1].strip()
            if prop not in wanted_props:
                continue
            cp_field = fields[0].strip()
            if ".." in cp_field:
                lo, hi = cp_field.split("..")
                result[prop].append((int(lo, 16), int(hi, 16)))
            else:
                cp = int(cp_field, 16)
                result[prop].append((cp, cp))
    for p in result:
        result[p] = merge_ranges(result[p])
    return result


def parse_unicode_data(path):
    """Returns (numeric_ranges, upper_map, lower_map) from UnicodeData.txt.
    numeric_ranges covers general category Nd (decimal digit); upper_map/
    lower_map are simple 1:1 case mappings (fields 12/13)."""
    nd_cps = []
    upper_map = {}
    lower_map = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            fields = line.rstrip("\n").split(";")
            if len(fields) < 15:
                continue
            cp = int(fields[0], 16)
            category = fields[2]
            simple_upper = fields[12].strip()
            simple_lower = fields[13].strip()
            if category == "Nd":
                nd_cps.append((cp, cp))
            if simple_upper:
                upper_map[cp] = int(simple_upper, 16)
            if simple_lower:
                lower_map[cp] = int(simple_lower, 16)
    return merge_ranges(nd_cps), upper_map, lower_map


def parse_case_folding(path):
    """Returns {cp: folded_cp} from CaseFolding.txt's common (C) and
    simple (S) status entries — skips full (F, multi-character) and
    Turkic-specific (T) mappings, matching char-foldcase's single-
    character contract."""
    fold_map = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = line.split(";")
            if len(fields) < 3:
                continue
            status = fields[1].strip()
            if status not in ("C", "S"):
                continue
            cp = int(fields[0].strip(), 16)
            folded = int(fields[2].strip(), 16)
            fold_map[cp] = folded
    return fold_map


def dict_to_pairs(mapping):
    return sorted((cp, to) for cp, to in mapping.items() if to != cp)


def emit_range_table(out, name, ranges):
    out.write(f"const uint32_t {name}[][2] = {{\n")
    for start, end in ranges:
        out.write(f"    {{0x{start:X}, 0x{end:X}}},\n")
    out.write("};\n")
    out.write(f"const size_t {name}_count = {len(ranges)};\n\n")


def emit_mapping_table(out, name, pairs):
    out.write(f"const uint32_t {name}_from[] = {{\n    ")
    out.write(", ".join(f"0x{cp:X}" for cp, _ in pairs))
    out.write("\n};\n")
    out.write(f"const uint32_t {name}_to[] = {{\n    ")
    out.write(", ".join(f"0x{cp:X}" for _, cp in pairs))
    out.write("\n};\n")
    out.write(f"const size_t {name}_count = {len(pairs)};\n\n")


def emit_range_decl(out, name):
    out.write(f"extern const uint32_t {name}[][2];\n")
    out.write(f"extern const size_t {name}_count;\n\n")


def emit_mapping_decl(out, name):
    out.write(f"extern const uint32_t {name}_from[];\n")
    out.write(f"extern const uint32_t {name}_to[];\n")
    out.write(f"extern const size_t {name}_count;\n\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="16.0.0",
                     help="Unicode version to fetch from unicode.org (default: 16.0.0)")
    args = ap.parse_args()
    version = args.version

    paths = fetch(version)

    derived = parse_prop_ranges(paths["DerivedCoreProperties.txt"],
                                 {"Alphabetic", "Uppercase", "Lowercase"})
    proplist = parse_prop_ranges(paths["PropList.txt"], {"White_Space"})
    numeric, upper_map, lower_map = parse_unicode_data(paths["UnicodeData.txt"])
    fold_map = parse_case_folding(paths["CaseFolding.txt"])

    alpha = derived["Alphabetic"]
    upper = derived["Uppercase"]
    lower = derived["Lowercase"]
    whitespace = proplist["White_Space"]

    upper_pairs = dict_to_pairs(upper_map)
    lower_pairs = dict_to_pairs(lower_map)
    fold_pairs = dict_to_pairs(fold_map)

    with open("src/unicode_tables.h", "w") as f:
        f.write(f"""#ifndef CURRY_UNICODE_TABLES_H
#define CURRY_UNICODE_TABLES_H

/* GENERATED FILE — do not hand-edit.
 * Produced by tools/gen-unicode-tables.py from the Unicode Character
 * Database version {version} (UnicodeData.txt, DerivedCoreProperties.txt,
 * PropList.txt, CaseFolding.txt — unicode.org/Public/{version}/ucd/).
 * Regenerate: python3 tools/gen-unicode-tables.py (run from repo root)
 *
 * Each range table is a sorted, non-overlapping array of inclusive
 * [start, end] codepoint pairs. Each mapping table is a pair of parallel
 * arrays (from[i] -> to[i]), sorted by from[], both length *_count.
 * Consumed by src/unicode.c via binary search.
 */

#include <stdint.h>
#include <stddef.h>

""")
        emit_range_decl(f, "unicode_alpha_ranges")
        emit_range_decl(f, "unicode_numeric_ranges")
        emit_range_decl(f, "unicode_space_ranges")
        emit_range_decl(f, "unicode_upper_ranges")
        emit_range_decl(f, "unicode_lower_ranges")
        emit_mapping_decl(f, "unicode_upper_map")
        emit_mapping_decl(f, "unicode_lower_map")
        emit_mapping_decl(f, "unicode_fold_map")
        f.write("#endif /* CURRY_UNICODE_TABLES_H */\n")

    with open("src/unicode_tables.c", "w") as out:
        out.write(f"""/* GENERATED FILE — do not hand-edit.
 * Produced by tools/gen-unicode-tables.py from the Unicode Character
 * Database version {version}. See unicode_tables.h for provenance.
 */

#include <stdint.h>
#include <stddef.h>
#include "unicode_tables.h"

""")
        emit_range_table(out, "unicode_alpha_ranges", alpha)
        emit_range_table(out, "unicode_numeric_ranges", numeric)
        emit_range_table(out, "unicode_space_ranges", whitespace)
        emit_range_table(out, "unicode_upper_ranges", upper)
        emit_range_table(out, "unicode_lower_ranges", lower)
        emit_mapping_table(out, "unicode_upper_map", upper_pairs)
        emit_mapping_table(out, "unicode_lower_map", lower_pairs)
        emit_mapping_table(out, "unicode_fold_map", fold_pairs)

    print(f"Wrote src/unicode_tables.h and src/unicode_tables.c "
          f"(Unicode {version}: {len(alpha)} alpha ranges, "
          f"{len(numeric)} numeric ranges, {len(whitespace)} space ranges, "
          f"{len(upper)} upper ranges, {len(lower)} lower ranges, "
          f"{len(upper_pairs)} upper mappings, {len(lower_pairs)} lower mappings, "
          f"{len(fold_pairs)} fold mappings)")


if __name__ == "__main__":
    main()
