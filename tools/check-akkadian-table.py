#!/usr/bin/env python3
"""Validate src/akkadian_names.h for correctness invariants.

Run after editing the table (manually or via batch authoring):

  python3 tools/check-akkadian-table.py

Checks, each a real correctness bug if violated:

1. No duplicate transliterated name across the whole table (AKK_SF+AKK_PR
   combined). The reader interns the *literal cuneiform/translit string* as
   a symbol; env_define for a later duplicate silently shadows the earlier
   one's binding (src/builtins.c's AKK_PR loop, in table order), making the
   earlier procedure/form permanently unreachable via that name.
2. No duplicate cuneiform string, same reasoning.
3. No entry contains whitespace in translit or cuneiform columns — the
   reader delimits tokens on whitespace, so such an entry could never be
   typed as a single token (see the removed syntax-rules bug, commit
   eb15ff6/31c71a9).
4. Every cuneiform codepoint falls in the Cuneiform Unicode block
   (U+12000-U+1247F) that SEX_IS_CUNEIFORM (src/numeric.h) checks against.
5. No translit/cuneiform name collides with an actual English builtin name
   already registered elsewhere in the table (would make two unrelated
   entries alias to whichever is processed first).
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "src" / "akkadian_names.h"

CUNEI_LO, CUNEI_HI = 0x12000, 0x1247F


def main():
    text = HEADER.read_text(encoding="utf-8")
    rows = re.findall(
        r'(AKK_SF|AKK_PR)\("([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\)', text)

    errors = []

    by_translit = defaultdict(list)
    by_cunei = defaultdict(list)
    all_english = set()

    for kind, eng, tr, cu in rows:
        all_english.add(eng)
        by_translit[tr].append((kind, eng))
        by_cunei[cu].append((kind, eng))

        if any(ch.isspace() for ch in tr):
            errors.append(f"whitespace in translit for {eng!r}: {tr!r}")
        if any(ch.isspace() for ch in cu):
            errors.append(f"whitespace in cuneiform for {eng!r}: {cu!r}")
        # The reader appends trailing non-delimiter ASCII (?, !, etc.) after
        # the cuneiform glyphs themselves (reader.c:569-577) — legitimate,
        # not a bug. Only the *leading run* of codepoints must be in-block;
        # once we leave the block we're in the ASCII suffix for good.
        i = 0
        while i < len(cu) and CUNEI_LO <= ord(cu[i]) <= CUNEI_HI:
            i += 1
        if i == 0:
            errors.append(
                f"cuneiform column for {eng!r} doesn't start with a "
                f"cuneiform codepoint: {cu!r}")
        for ch in cu[i:]:
            if CUNEI_LO <= ord(ch) <= CUNEI_HI:
                errors.append(
                    f"cuneiform codepoint after ASCII suffix began, in "
                    f"column for {eng!r}: {cu!r}")
            if ch.isalnum() and not ch.isascii():
                errors.append(
                    f"non-ASCII, non-cuneiform codepoint U+{ord(ch):04X} "
                    f"({ch!r}) in cuneiform column for {eng!r}: {cu!r}")

    # A name registered identically under both AKK_SF and AKK_PR for the
    # SAME english procedure (spawn/send!/receive are both a special form
    # per symbol_list.h and a plain procedure) is a harmless no-op re-bind,
    # not a shadowing bug — only flag collisions across DIFFERENT english
    # names.
    for tr, entries in by_translit.items():
        distinct_english = {eng for _, eng in entries}
        if len(distinct_english) > 1:
            errors.append(
                f"duplicate translit {tr!r} used by different names: "
                + ", ".join(f"{k}:{e}" for k, e in entries))
    for cu, entries in by_cunei.items():
        distinct_english = {eng for _, eng in entries}
        if len(distinct_english) > 1:
            errors.append(
                f"duplicate cuneiform {cu!r} used by different names: "
                + ", ".join(f"{k}:{e}" for k, e in entries))

    # Check 5: translit/cuneiform colliding with an unrelated english name.
    for tr, entries in by_translit.items():
        if tr in all_english and all(eng != tr for _, eng in entries):
            errors.append(
                f"translit {tr!r} collides with a different builtin's "
                f"english name (used by {entries})")
    for cu, entries in by_cunei.items():
        if cu in all_english and all(eng != cu for _, eng in entries):
            errors.append(
                f"cuneiform {cu!r} collides with a different builtin's "
                f"english name (used by {entries})")

    print(f"Parsed {len(rows)} AKK_SF/AKK_PR rows "
          f"({sum(1 for k,_,_,_ in rows if k=='AKK_SF')} SF, "
          f"{sum(1 for k,_,_,_ in rows if k=='AKK_PR')} PR)")

    if errors:
        print(f"\n{len(errors)} problem(s):")
        for e in errors:
            print(" -", e)
        return 1

    print("All invariants hold: no duplicates, no whitespace, "
          "all cuneiform in-block, no english-name collisions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
