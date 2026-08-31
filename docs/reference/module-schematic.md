# Module: (curry schematic)

*unreleased*

A suite of small pure-Scheme developer tools for Scheme source — a reindenter, a commented-definition extractor, and Markdown/svnwiki documentation generators built on it — ported from Evan Hanson's BSD-licensed [schematic](https://git.foldling.org/schematic/). No C, no external dependency.

Unlike most `(curry X)` modules, this is a family of five separate sub-libraries rather than one flat module, mirroring upstream's own `(schematic read)`/`(schematic extract)`/`(schematic format)` split:

| Sub-library | Import | What it does |
|---|---|---|
| [read](#curry-schematic-read) | `(curry schematic read)` | Splits source into alternating comment/code "sections" — the shared primitive everything else builds on |
| [extract](#curry-schematic-extract) | `(curry schematic extract)` | Recognizes commented definitions and emits s-expressive specifications |
| [format](#curry-schematic-format) | `(curry schematic format)` | Reindents Scheme source (not a pretty-printer — never changes line breaks or intraline spacing) |
| [markdown](#curry-schematic-markdown) | `(curry schematic markdown)` | Comment/code sections → Markdown prose + indented code blocks |
| [wiki](#curry-schematic-wiki) | `(curry schematic wiki)` | Extracted specifications → CHICKEN-wiki svnwiki tags |

Runnable CLI wrappers for all five are in `examples/schematic/schematic-{format,extract,markdown,wiki}.scm`, using `(curry getopt)` for argument parsing (a different mechanism from upstream's own CLI programs, which use a separate `optimism`/`command-line-options` library curry doesn't need — `(curry getopt)` already covers the same ground).

## A note on this port

This port predates `(scheme case-lambda)` (see `lib/curry/modules/scheme/case-lambda.sld`), which didn't exist yet when it was written. Every one of upstream's `case-lambda` procedures is rewritten here to take its optional trailing arguments via a `. rest` list instead — the same convention `(curry csv)`/`(curry toml)` already use for their own optional arguments — and hasn't been migrated back to real `case-lambda` since the existing dispatch already works.

`(curry schematic extract)` writes its own small, local pattern matcher (a `#(...)`-wrapped pattern position captures; anything else must `equal?` the input) rather than reusing `(curry matchable)` — it's upstream's own tiny, purpose-built matcher for a small fixed pattern set, kept as its own thing so this port's behavior stays directly traceable against upstream rather than needing every clause re-derived against a different pattern language's literal-vs-capture rules.

## Scope

Only reasonably simple definition styles are recognized by `(curry schematic extract)` (and, transitively, `(curry schematic wiki)`) — this is upstream's own caveat, not something the port relaxed. "Let over lambda" idioms are silently skipped, and definitions not at the toplevel are ignored unless they occur inside a recognized library-wrapper form (R7RS `define-library`, Gauche `define-module`, or CHICKEN `module`).

`(curry schematic format)`'s own outstanding TODO, inherited as-is from upstream: `=>` in `cond` forms should not open a new indentation level, but currently does.

## (curry schematic read)

### `(port-fold-source-sections kons knil comment-prefixes port)`

Folds `kons` over every comment/code section read from `port`, in order — `kons` is called as `(kons comment-text code-text acc)` once per section, where a "section" is a maximal run of comment lines immediately followed by a run of code lines, or vice versa. Both texts have their comment-prefix markers stripped and leading/trailing blank lines trimmed. `comment-prefixes` is a list of line-comment prefix strings (curry's own convention: `'(";;;" ";;")`).

`kons` is always called at least once, even for a completely empty document (with both texts `""`) — an unconditional final call at end-of-input, matching upstream's own structure exactly.

```scheme
(import (curry schematic read))
(port-fold-source-sections
  (lambda (doc code acc) (cons (cons doc code) acc))
  '() '(";;;" ";;")
  (open-input-string ";; Adds two numbers.\n(define (add a b) (+ a b))\n"))
; => (("Adds two numbers." . "(define (add a b) (+ a b))"))
```

## (curry schematic extract)

### `(extract-definitions . opts)`

`opts` is `[input [output [types [comment-prefixes]]]]`, each defaulting to `(current-input-port)`, `(current-output-port)`, `#f`, and `'(";;;" ";;")` respectively. Scans `input` for commented definitions and writes one s-expressive specification per comment block to `output`:

```
<specification> = (<comment> (<type> . <form>) ...)
<comment>       = string?
<form>          = any?
<type>          = 'procedure | 'syntax | 'constant | 'parameter
                | 'record | 'string | 'type | 'declaration
```

Recognized forms: `define` (procedure, lambda-valued, `case-lambda`-valued, `make-parameter`-valued, or a plain constant/string value), `define-syntax` (with or without a `syntax-rules` transformer, in which case one `'syntax` spec is emitted per clause), `define-record-type` (emits `'record` plus one `'procedure` spec per constructor/predicate/accessor/mutator), the CHICKEN-specific `define-record` shorthand, `define-type`, `:` type declarations, and `declare`.

`types` selects between two ways of describing a procedure that has both a `:` type declaration and an ordinary `define` immediately following it under the same comment: `#f` (default) describes it by the `define` form itself, skipping the `:` declaration entirely; `#t` uses the `:` declaration's argument types instead, and — matching upstream's own structure exactly — *also* still emits a second spec from the subsequent `define`, since nothing in the `:` clause suppresses the outer loop's own next iteration.

```scheme
(import (curry schematic extract))
(define out (open-output-string))
(extract-definitions (open-input-string ";; Adds two numbers.\n(define (add a b) (+ a b))\n") out)
(get-output-string out)
; => "(\"Adds two numbers.\" (procedure add a b))\n"
```

## (curry schematic format)

### `(format-scheme . opts)`

`opts` is `[input [output [custom-keyword-indent]]]`, each defaulting to `(current-input-port)`, `(current-output-port)`, and `keyword-indent`. Reads Scheme source from `input`, reindents it, and writes the result to `output`. This is **not** a pretty-printer — it never introduces line breaks or changes intraline spacing, only line indentation.

### `(keyword-indent sym eol?)`

Determines the horizontal alignment of a keyword's subforms: a numerical offset from the keyword's own column, a list of offsets (applied to the form's data in order, the last persisting until the form closes — e.g. `do`'s `'(3 3 1)`), or `#f` for no special treatment. `eol?` is whether the keyword was the last token on its own line (only `begin`/`cond` care). `custom-keyword-indent` (above) is any procedure matching this same two-argument calling convention — pass your own to override or extend the built-in rules (see `examples/schematic/schematic-format.scm` for a worked example loading rules from a file).

### Parameters

- `(bracket-closure? [bool])` — when true, a single closing bracket (`]`) makes `format-scheme` insert closing parentheses for every open form before continuing (recovering from a dropped-paren typo mid-edit).
- `(bracket-parentheses? [bool])` — when true, brackets are treated exactly like parentheses. Supersedes `bracket-closure?` when both are set.
- `(tabstop-length [n-or-#f])` — when set, indents with tabs of this width first, then spaces for any remaining columns; `#f` (default) indents with spaces only.

```scheme
(import (curry schematic format))
(define out (open-output-string))
(format-scheme (open-input-string "(let ((a 1)\n(b 2))\n(+ a b))\n") out)
(get-output-string out)
; => "(let ((a 1)\n      (b 2))\n  (+ a b))\n"
```

## (curry schematic markdown)

### `(scheme->markdown input output . opts)`

`opts` is `[comment-prefixes]` (default `'(";;;" ";;")`). Converts the Scheme source on `input` to Markdown on `output`: each comment/code section from `port-fold-source-sections` becomes a paragraph of prose followed by a 4-space-indented code block. Doesn't use `(curry schematic extract)` at all — no definition-shape recognition happens here, matching upstream's own design.

## (curry schematic wiki)

### `(scheme->wiki input output . opts)`

`opts` is `[types [comment-prefixes]]`, passed straight through to `extract-definitions`. Converts the Scheme source on `input` to [svnwiki](http://wiki.call-cc.org/edit-help) documentation fragments on `output`, suitable for the CHICKEN wiki: `'procedure`/`'syntax`/`'constant`/`'parameter`/`'record`/`'string`/`'type` specs become `<tag>form</tag>` markup, and anything else (currently just `'declaration`) becomes a plain `" type form"` line. Any limitations of `(curry schematic extract)` apply equally here.

```scheme
(import (curry schematic wiki))
(define out (open-output-string))
(scheme->wiki (open-input-string ";; Adds two numbers.\n(define (add a b) (+ a b))\n") out)
(get-output-string out)
; => "<procedure>(add a b)</procedure>\n\nAdds two numbers.\n\n"
```

## See also

- [`module-matchable.md`](module-matchable.md) — the "porting a hygiene-dependent/pattern-matching algorithm to curry" discussion there doesn't apply here (extract's own matcher is a simple `equal?`-based one, not macro-hygiene-dependent), but is worth reading if extending extract's own matcher ever seems tempting.
- [`module-getopt.md`](module-getopt.md) — the option parser the example CLI scripts in `examples/schematic/` use.
