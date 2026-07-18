#!/usr/bin/env python3
"""Generate editor syntax-highlighting grammars for Curry Scheme.

Parses the authoritative name tables in the C sources:

  src/symbol_list.h      — pre-interned special-form symbols
  src/akkadian_names.h   — AKK_SF/AKK_PR three-language synonym table
  src/*.c                — DEF("name", ...) and cond_def(env, "name", ...)
                           builtin registrations

and emits three grammars that stay in sync with the runtime:

  editors/vim/syntax/curry.vim
  editors/kate/curry.xml               (KSyntaxHighlighting: Kate, KWrite,
                                        KDevelop, Qt Creator)
  editors/vscode/syntaxes/curry.tmLanguage.json

Re-run after adding builtins or Akkadian synonyms:

  python3 tools/gen-editor-syntax.py

Highlighting rules mirror the reader's own priorities: Akkadian cuneiform
synonyms win over sexagesimal-numeral interpretation (𒁹 is `define`, not 1;
𒁹𒁹𒁹 is `newline`, not 3), so synonym rules must outrank the cuneiform
number rule in every grammar.
"""

import json
import re
import sys
from pathlib import Path
from xml.sax.saxutils import escape as xml_escape

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
EDITORS = ROOT / "editors"

# Scheme token delimiters (per reader.c): whitespace ( ) [ ] " ; ' ` ,
DELIM_CLASS = r"""\s()\[\]'"`,;"""

# ASCII characters allowed in vim 'iskeyword' for `syn keyword` use.
# Matches the iskeyword line emitted into curry.vim below.
VIM_KW_CHARS = set(
    "!$%&*+-./:<=>?@^_~"
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
)

CUNEI_LO, CUNEI_HI = 0x12000, 0x1247F


def is_cuneiform(name: str) -> bool:
    return any(CUNEI_LO <= ord(c) <= CUNEI_HI for c in name)


# Words `syn keyword` would parse as options rather than keywords.
VIM_SYN_OPTIONS = {
    "contains", "contained", "containedin", "nextgroup", "transparent",
    "skipwhite", "skipnl", "skipempty", "conceal", "cchar", "oneline",
    "fold", "display", "extend", "keepend", "excludenl", "matchgroup",
}


def vim_keyword_ok(name: str) -> bool:
    return name not in VIM_SYN_OPTIONS and all(c in VIM_KW_CHARS for c in name)


# ---------------------------------------------------------------- parsing

def parse_symbol_list() -> set:
    """Special-form names from symbol_list.h (minus non-form entries)."""
    text = (SRC / "symbol_list.h").read_text(encoding="utf-8")
    names = set()
    for var, name in re.findall(r'SYM\((\w+),\s*"([^"]+)"\)', text):
        if var.startswith("EC_"):
            continue                      # error codes, not source tokens
        if name.startswith("**") or name.startswith("#:"):
            continue                      # profiler globals / keyword objects
        if name in {".", "_"}:
            continue
        names.add(name)
    return names


def parse_akkadian():
    """(english, translit, cuneiform) rows, split by SF/PR."""
    text = (SRC / "akkadian_names.h").read_text(encoding="utf-8")
    rows = re.findall(
        r'(AKK_SF|AKK_PR)\("([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\)', text)
    sf, pr = [], []
    for kind, eng, tr, cu in rows:
        (sf if kind == "AKK_SF" else pr).append((eng, tr, cu))
    return sf, pr


def parse_builtins() -> set:
    """Every DEF("name",...) / cond_def(env,"name",...) across src/*.c."""
    names = set()
    pat = re.compile(r'\b(?:DEF|cond_def)\s*\(\s*(?:env\s*,\s*)?"([^"]+)"')
    for c in sorted(SRC.glob("*.c")):
        for name in pat.findall(c.read_text(encoding="utf-8")):
            names.add(name)
    return names


# ------------------------------------------------------------- name pools

def build_pools():
    forms = parse_symbol_list()
    akk_sf, akk_pr = parse_akkadian()
    builtins = parse_builtins()

    # A name registered as a procedure is a procedure, even if pre-interned
    # (apply, map, error, values, ...).
    forms -= builtins

    pools = {
        "forms":        sorted(forms),
        "builtins":     sorted(builtins),
        # Akkadian synonyms, transliterated + cuneiform in one pool per class
        "akk_forms":    sorted({t for _, t, _ in akk_sf} |
                               {c for _, _, c in akk_sf}),
        "akk_builtins": sorted({t for _, t, _ in akk_pr} |
                               {c for _, _, c in akk_pr}),
    }
    # A synonym used for both a form and a procedure (spawn/send!/receive
    # appear in both tables) counts as a form.
    pools["akk_builtins"] = [n for n in pools["akk_builtins"]
                             if n not in set(pools["akk_forms"])]
    return pools


def by_length(names):
    """Longest first, so regex alternation never stops at a prefix."""
    return sorted(names, key=lambda s: (-len(s), s))


def chunks(seq, n):
    for i in range(0, len(seq), n):
        yield seq[i:i + n]


# ------------------------------------------------------------------- vim

VIM_MAGIC = set(r"\/~*.[]^$")           # special in magic-mode patterns


def vim_escape(name: str) -> str:
    return "".join("\\" + c if c in VIM_MAGIC else c for c in name)


def vim_match_lines(group: str, names, per_chunk=40):
    """`syn match` alternation lines with Scheme-token boundaries."""
    pre = r"\%(^\|[[:space:]()\[\]'`\",;]\)\@<="
    post = r"\%([[:space:]()\[\]'`\",;]\|$\)\@="
    out = []
    for ch in chunks(by_length(names), per_chunk):
        alt = r"\|".join(vim_escape(n) for n in ch)
        out.append(f'syn match {group} /{pre}\\%({alt}\\){post}/')
    return out


def emit_vim(pools) -> str:
    kw_forms = [n for n in pools["forms"] if vim_keyword_ok(n)]
    m_forms = [n for n in pools["forms"] if not vim_keyword_ok(n)]
    kw_builtins = [n for n in pools["builtins"] if vim_keyword_ok(n)]
    m_builtins = [n for n in pools["builtins"] if not vim_keyword_ok(n)]

    L = []
    a = L.append
    a('" Vim syntax file')
    a('" Language:    Curry Scheme')
    a('" Maintainer:  generated by tools/gen-editor-syntax.py — do not edit')
    a('')
    a('if exists("b:current_syntax")')
    a('  finish')
    a('endif')
    a('')
    a('" R7RS identifier characters (excludes , ; \' ` ")')
    a('syn iskeyword 33,36-38,42-43,45-58,60-64,65-90,94-95,97-122,126')
    a('')
    a('" ---- comments -------------------------------------------------')
    a('syn match   curryComment /;.*$/ contains=@Spell')
    a('syn match   curryComment /#!.*$/')
    a('syn region  curryBlockComment start=/#|/ end=/|#/ contains=curryBlockComment')
    a('syn match   curryDatumComment /#;/')
    a('')
    a('" ---- strings and characters -----------------------------------')
    a(r'syn region  curryString start=/"/ skip=/\\\\\|\\"/ end=/"/ contains=curryStringEscape')
    a(r'syn match   curryStringEscape /\\\%([abntr\\"]\|x\x\+;\?\)/ contained')
    a(r'syn match   curryChar /#\\\%(x\x\+\|alarm\|backspace\|delete\|escape\|newline\|null\|return\|space\|tab\|.\)/')
    a('')
    a('" ---- booleans, keyword objects, quoting -----------------------')
    a(r'syn match   curryBoolean /#\%(true\|false\|t\|f\)\%([[:space:]()\[\]\x27`",;]\|$\)\@=/')
    a(r'syn match   curryKeywordObj /#:[^[:space:]()\[\]\x27`",;]\+/')
    a(r"syn match   curryQuote /['`]\|,@\?/")
    a('')
    a('" ---- numbers ---------------------------------------------------')
    pre = r'\%(^\|[[:space:]()\[\]\x27`",;]\)\@<='
    post = r'\%([[:space:]()\[\]\x27`",;]\|$\)\@='
    a(f'syn match   curryNumber /{pre}[-+]\\?\\d\\+\\%(\\/\\d\\+\\|\\.\\d*\\%([eE][-+]\\?\\d\\+\\)\\?\\)\\?{post}/')
    a(r'syn match   curryRadixNumber /#[bodxBODX][-+]\?[0-9a-fA-F]\+\%(\/[0-9a-fA-F]\+\)\?/')
    a('" Neugebauer sexagesimal: #s1;30 = 3/2, #s1,0,0 = 3600')
    a(r'syn match   currySexagesimal /#[sS]\d\+\%(,\d\+\)*\%(;\d\+\%(,\d\+\)*\)\?/')
    a('" Cuneiform numerals 𒁹=1 𒌋=10 𒑊=0.  The reader merges single-space-')
    a('" separated glyph groups into ONE token, then checks the synonym table,')
    a('" then parses as a sexagesimal number.  So a lone group defers to a')
    a('" synonym (𒁹 is `define`, 𒁹𒁹𒁹 is `newline`) but a spaced multi-group')
    a('" run is always a number (𒁹 𒌋𒁹 = 71).  Vim priority is definition')
    a('" order (later wins at the same position): single-group number here,')
    a('" synonyms after it, multi-group number last.')
    a(f'syn match   curryCuneiformNumber /{pre}[𒁹𒌋𒑊]\\+{post}/')
    a('')
    a('" ---- special forms ---------------------------------------------')
    for ch in chunks(kw_forms, 12):
        a('syn keyword curryForm ' + ' '.join(ch))
    for line in vim_match_lines('curryForm', m_forms):
        a(line)
    a('')
    a('" ---- builtin procedures ----------------------------------------')
    for ch in chunks(kw_builtins, 12):
        a('syn keyword curryBuiltin ' + ' '.join(ch))
    for line in vim_match_lines('curryBuiltin', m_builtins):
        a(line)
    a('')
    a('" ---- Akkadian / cuneiform synonyms -----------------------------')
    for line in vim_match_lines('curryAkkForm', pools["akk_forms"]):
        a(line)
    for line in vim_match_lines('curryAkkBuiltin', pools["akk_builtins"]):
        a(line)
    a('')
    a('" Multi-group spaced numeral — outranks synonyms (defined last)')
    a(f'syn match   curryCuneiformNumber /{pre}[𒁹𒌋𒑊]\\+\\%( [𒁹𒌋𒑊]\\+\\)\\+{post}/')
    a('')
    a('syn sync fromstart')
    a('')
    a('hi def link curryComment          Comment')
    a('hi def link curryBlockComment     Comment')
    a('hi def link curryDatumComment     Comment')
    a('hi def link curryString           String')
    a('hi def link curryStringEscape     SpecialChar')
    a('hi def link curryChar             Character')
    a('hi def link curryBoolean          Boolean')
    a('hi def link curryKeywordObj       Special')
    a('hi def link curryQuote            Special')
    a('hi def link curryNumber           Number')
    a('hi def link curryRadixNumber      Number')
    a('hi def link currySexagesimal      Number')
    a('hi def link curryCuneiformNumber  Number')
    a('hi def link curryForm             Statement')
    a('hi def link curryAkkForm          Statement')
    a('hi def link curryBuiltin          Function')
    a('hi def link curryAkkBuiltin       Function')
    a('')
    a('let b:current_syntax = "curry"')
    a('')
    return "\n".join(L)


# ------------------------------------------------------------------ kate

def emit_kate(pools) -> str:
    def items(names):
        return "\n".join(f"      <item>{xml_escape(n)}</item>"
                         for n in sorted(names))

    delim_in = xml_escape(r"""[^\s()\[\]'"`,;]""", {'"': "&quot;"})
    L = f'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE language SYSTEM "language.dtd">
<!-- Generated by tools/gen-editor-syntax.py — do not edit.
     Works in Kate, KWrite, KDevelop, Qt Creator (KSyntaxHighlighting). -->
<language name="Curry Scheme" version="1" kateversion="5.0" section="Scripts"
          extensions="*.scm;*.sld" priority="9"
          author="curry" license="MIT">
  <highlighting>
    <list name="forms">
{items(pools["forms"])}
    </list>
    <list name="akkforms">
{items(pools["akk_forms"])}
    </list>
    <list name="builtins">
{items(pools["builtins"])}
    </list>
    <list name="akkbuiltins">
{items(pools["akk_builtins"])}
    </list>
    <contexts>
      <context name="Normal" attribute="Normal" lineEndContext="#stay">
        <DetectSpaces/>
        <StringDetect attribute="Comment" context="BlockComment" String="#|" beginRegion="comment"/>
        <Detect2Chars attribute="Comment" context="#stay" char="#" char1=";"/>
        <RegExpr attribute="Comment" context="#stay" String="#!.*$"/>
        <DetectChar attribute="Comment" context="LineComment" char=";"/>
        <DetectChar attribute="String" context="String" char="&quot;"/>
        <RegExpr attribute="Char" context="#stay" String="#\\\\(?:x[0-9A-Fa-f]+|alarm|backspace|delete|escape|newline|null|return|space|tab|.)"/>
        <RegExpr attribute="Boolean" context="#stay" String="#(?:true|false|t|f)(?!{delim_in})"/>
        <RegExpr attribute="Sexagesimal" context="#stay" String="#[sS][0-9]+(?:,[0-9]+)*(?:;[0-9]+(?:,[0-9]+)*)?"/>
        <RegExpr attribute="BaseN" context="#stay" String="#[bodxBODX][+-]?[0-9A-Fa-f]+(?:/[0-9A-Fa-f]+)?"/>
        <RegExpr attribute="KeywordObj" context="#stay" String="#:{delim_in}+"/>
        <!-- Multi-group spaced cuneiform numeral (𒁹 𒌋𒁹 = 71) merges into
             one reader token, so it outranks the synonym lists; a lone
             group defers to them (𒁹 is `define`, not 1). -->
        <RegExpr attribute="CuneiformNum" context="#stay" String="[\\x{{12079}}\\x{{1230B}}\\x{{1244A}}]+(?: [\\x{{12079}}\\x{{1230B}}\\x{{1244A}}]+)+(?![\\x{{12000}}-\\x{{1247F}}])"/>
        <keyword attribute="Keyword" context="#stay" String="forms"/>
        <keyword attribute="AkkKeyword" context="#stay" String="akkforms"/>
        <keyword attribute="Builtin" context="#stay" String="builtins"/>
        <keyword attribute="AkkBuiltin" context="#stay" String="akkbuiltins"/>
        <RegExpr attribute="CuneiformNum" context="#stay" String="[\\x{{12079}}\\x{{1230B}}\\x{{1244A}}]+(?![\\x{{12000}}-\\x{{1247F}}])"/>
        <RegExpr attribute="Number" context="#stay" String="(?&lt;!{delim_in})[+-]?[0-9]+(?:/[0-9]+|\\.[0-9]*(?:[eE][+-]?[0-9]+)?)?(?!{delim_in})"/>
        <Detect2Chars attribute="Quote" context="#stay" char="," char1="@"/>
        <AnyChar attribute="Quote" context="#stay" String="'`,"/>
      </context>
      <context name="LineComment" attribute="Comment" lineEndContext="#pop"/>
      <context name="BlockComment" attribute="Comment" lineEndContext="#stay">
        <StringDetect attribute="Comment" context="BlockComment" String="#|" beginRegion="comment"/>
        <StringDetect attribute="Comment" context="#pop" String="|#" endRegion="comment"/>
      </context>
      <context name="String" attribute="String" lineEndContext="#stay">
        <RegExpr attribute="StringEscape" context="#stay" String="\\\\(?:[abntr\\\\&quot;]|x[0-9A-Fa-f]+;?)"/>
        <DetectChar attribute="String" context="#pop" char="&quot;"/>
      </context>
    </contexts>
    <itemDatas>
      <itemData name="Normal"        defStyleNum="dsNormal"/>
      <itemData name="Keyword"       defStyleNum="dsKeyword"/>
      <itemData name="AkkKeyword"    defStyleNum="dsKeyword"/>
      <itemData name="Builtin"       defStyleNum="dsBuiltIn"/>
      <itemData name="AkkBuiltin"    defStyleNum="dsBuiltIn"/>
      <itemData name="Comment"       defStyleNum="dsComment"/>
      <itemData name="String"        defStyleNum="dsString"/>
      <itemData name="StringEscape"  defStyleNum="dsSpecialChar"/>
      <itemData name="Char"          defStyleNum="dsChar"/>
      <itemData name="Boolean"       defStyleNum="dsConstant"/>
      <itemData name="Number"        defStyleNum="dsDecVal"/>
      <itemData name="BaseN"         defStyleNum="dsBaseN"/>
      <itemData name="Sexagesimal"   defStyleNum="dsBaseN"/>
      <itemData name="CuneiformNum"  defStyleNum="dsBaseN"/>
      <itemData name="KeywordObj"    defStyleNum="dsOthers"/>
      <itemData name="Quote"         defStyleNum="dsSpecialChar"/>
    </itemDatas>
  </highlighting>
  <general>
    <comments>
      <comment name="singleLine" start=";"/>
      <comment name="multiLine" start="#|" end="|#"/>
    </comments>
    <keywords casesensitive="1"
              weakDeliminator="!?+-*/&lt;&gt;=:%^~@$._&amp;"
              additionalDeliminator="'`&quot;"/>
  </general>
</language>
'''
    return L


# ------------------------------------------------------------- textmate

def tm_word_pattern(names, scope):
    """One pattern object with token-boundary lookarounds."""
    alt = "|".join(re.escape(n) for n in by_length(names))
    return {
        "match": f"(?<![^{DELIM_CLASS}])(?:{alt})(?![^{DELIM_CLASS}])",
        "name": scope,
    }


def tm_word_patterns(names, scope, per_chunk=120):
    return [tm_word_pattern(ch, scope)
            for ch in chunks(by_length(names), per_chunk)]


def emit_tm(pools) -> str:
    D = DELIM_CLASS
    patterns = [
        {"begin": r"#\|", "end": r"\|#", "name": "comment.block.curry",
         "patterns": [{"include": "#blockcomment"}]},
        {"match": r"#;", "name": "comment.line.datum.curry"},
        {"match": r"#!.*$", "name": "comment.line.shebang.curry"},
        {"match": r";.*$", "name": "comment.line.semicolon.curry"},
        {"begin": '"', "end": '"', "name": "string.quoted.double.curry",
         "patterns": [
             {"match": r'\\(?:[abntr\\"]|x[0-9A-Fa-f]+;?)',
              "name": "constant.character.escape.curry"}]},
        {"match": r"#\\(?:x[0-9A-Fa-f]+|alarm|backspace|delete|escape"
                  r"|newline|null|return|space|tab|.)",
         "name": "constant.character.curry"},
        {"match": rf"#(?:true|false|t|f)(?![^{D}])",
         "name": "constant.language.boolean.curry"},
        {"match": r"#[sS][0-9]+(?:,[0-9]+)*(?:;[0-9]+(?:,[0-9]+)*)?",
         "name": "constant.numeric.sexagesimal.curry"},
        {"match": r"#[bodxBODX][+-]?[0-9A-Fa-f]+(?:/[0-9A-Fa-f]+)?",
         "name": "constant.numeric.radix.curry"},
        {"match": rf"#:[^{D}]+", "name": "constant.other.keyword.curry"},
    ]
    # Reader priority: a single-space-joined multi-group cuneiform run is
    # one token and always a number (𒁹 𒌋𒁹 = 71) — before the synonyms;
    # a lone group defers to the synonym table (𒁹 = define) — after them.
    cunei_digit = "[𒁹𒌋𒑊]"
    cunei_any = "[\U00012000-\U0001247F]"
    patterns.append(
        {"match": f"{cunei_digit}+(?: {cunei_digit}+)+(?!{cunei_any})",
         "name": "constant.numeric.cuneiform.curry"})
    patterns += tm_word_patterns(pools["akk_forms"],
                                 "keyword.control.akkadian.curry")
    patterns += tm_word_patterns(pools["akk_builtins"],
                                 "support.function.akkadian.curry")
    patterns.append(
        {"match": f"{cunei_digit}+(?!{cunei_any})",
         "name": "constant.numeric.cuneiform.curry"})
    patterns.append(
        {"match": rf"(?<![^{D}])[+-]?[0-9]+(?:/[0-9]+"
                  rf"|\.[0-9]*(?:[eE][+-]?[0-9]+)?)?(?![^{D}])",
         "name": "constant.numeric.curry"})
    patterns += tm_word_patterns(pools["forms"], "keyword.control.curry")
    patterns += tm_word_patterns(pools["builtins"], "support.function.curry")
    patterns.append({"match": r"'|`|,@|,",
                     "name": "keyword.operator.quote.curry"})

    grammar = {
        "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/"
                   "master/tmlanguage.json",
        "name": "Curry Scheme",
        "scopeName": "source.curry",
        "comment": "Generated by tools/gen-editor-syntax.py — do not edit.",
        "patterns": patterns,
        "repository": {
            "blockcomment": {
                "begin": r"#\|", "end": r"\|#",
                "name": "comment.block.curry",
                "patterns": [{"include": "#blockcomment"}],
            }
        },
    }
    return json.dumps(grammar, ensure_ascii=False, indent=2) + "\n"


# ------------------------------------------------------------------ main

def main():
    pools = build_pools()

    outputs = {
        EDITORS / "vim" / "syntax" / "curry.vim": emit_vim(pools),
        EDITORS / "kate" / "curry.xml": emit_kate(pools),
        EDITORS / "vscode" / "syntaxes" / "curry.tmLanguage.json":
            emit_tm(pools),
    }
    for path, text in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        print(f"wrote {path.relative_to(ROOT)}")

    print(f"special forms: {len(pools['forms'])}, "
          f"builtins: {len(pools['builtins'])}, "
          f"akkadian forms: {len(pools['akk_forms'])}, "
          f"akkadian builtins: {len(pools['akk_builtins'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
