#!/usr/bin/env python3
"""gen-width-table.py -- regenerate the terminal-width tables used by
(srfi 272 measure) from the Unicode Character Database.

Usage:
    python3 -I tools/gen-width-table.py [--version 16.0.0] [--cache DIR] \
        > lib/curry/modules/srfi/s272/width-data.scm

Sources (downloaded once into --cache, default .unicode-cache/):
  - EastAsianWidth.txt           UAX #11: W and F are 2 columns
  - extracted/DerivedGeneralCategory.txt
                                 Mn, Me, Cf are 0 columns; Cc is unprintable
  - emoji/emoji-data.txt         Emoji_Presentation is 2 columns

Cuneiform (U+12000..U+1254F) is East Asian Width N, so it is 1 column.
Only the Akkadian transliteration marks (combining macron, circumflex,
dot below, ...) are 0 columns, via Mn.
"""

import argparse
import os
import sys
import urllib.request

UCD_BASE = "https://www.unicode.org/Public/{version}/ucd/"
FILES = [
    "EastAsianWidth.txt",
    "extracted/DerivedGeneralCategory.txt",
    "emoji/emoji-data.txt",
]


def fetch(version, cache):
    os.makedirs(cache, exist_ok=True)
    paths = {}
    for name in FILES:
        path = os.path.join(cache, f"{version}-{name.replace('/', '_')}")
        if not os.path.exists(path):
            url = UCD_BASE.format(version=version) + name
            print(f"Downloading {url}...", file=sys.stderr)
            urllib.request.urlretrieve(url, path)
        paths[name] = path
    return paths


def parse_ranges(path, wanted):
    """Yield (lo, hi, value) for lines whose value is in `wanted`."""
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            cp, _, val = line.partition(";")
            val = val.strip().split()[0] if val.strip() else ""
            if val not in wanted:
                continue
            cp = cp.strip()
            if ".." in cp:
                lo, hi = cp.split("..")
            else:
                lo = hi = cp
            out.append((int(lo, 16), int(hi, 16), val))
    return out


def merge(ranges):
    ranges = sorted(ranges)
    merged = []
    for lo, hi in ranges:
        if merged and lo <= merged[-1][1] + 1:
            merged[-1][1] = max(merged[-1][1], hi)
        else:
            merged.append([lo, hi])
    return merged


def emit(name, ranges):
    print(f"(define {name}")
    print("  #(")
    for lo, hi in ranges:
        print(f"    #x{lo:X} #x{hi:X}")
    print("  ))")
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="16.0.0")
    ap.add_argument("--cache", default=".unicode-cache")
    args = ap.parse_args()
    paths = fetch(args.version, args.cache)

    wide = [(lo, hi) for lo, hi, _ in parse_ranges(paths["EastAsianWidth.txt"], {"W", "F"})]
    # Emoji_Presentation characters render as 2 columns even where UAX #11
    # classes them Neutral/Narrow (e.g. U+231A WATCH is already W; the
    # property fills any gaps).
    wide += [(lo, hi) for lo, hi, _ in parse_ranges(paths["emoji/emoji-data.txt"], {"Emoji_Presentation"})]
    zero = [(lo, hi) for lo, hi, _ in parse_ranges(paths["extracted/DerivedGeneralCategory.txt"], {"Mn", "Me", "Cf"})]
    control = [(lo, hi) for lo, hi, _ in parse_ranges(paths["extracted/DerivedGeneralCategory.txt"], {"Cc"})]
    # Soft hyphen (Cf) is conventionally 1 column in terminals; keep it
    # printable by removing it from the zero-width set.
    zero = [(lo, hi) for lo, hi in zero if not (lo == hi == 0xAD)]
    # Hangul Jamo medial vowels / final consonants (U+1160..U+11FF) are
    # Mn-free "N" but combine with a preceding initial consonant: 0 columns.
    zero.append((0x1160, 0x11FF))

    print(";;; GENERATED FILE -- do not hand-edit.")
    print(f";;; Produced by tools/gen-width-table.py from the Unicode Character")
    print(f";;; Database version {args.version}.")
    print(";;; Each table is a flat vector of sorted, non-overlapping inclusive")
    print(";;; (lo hi lo hi ...) codepoint ranges.")
    print()
    print("(define-library (srfi s272 width-data)")
    print("  (import (scheme base))")
    print("  (export %wide-ranges %zero-width-ranges %control-ranges)")
    print("  (begin")
    print()
    emit("%wide-ranges", merge(wide))
    emit("%zero-width-ranges", merge(zero))
    emit("%control-ranges", merge(control))
    print("  ))")


if __name__ == "__main__":
    main()
