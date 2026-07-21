# Module: `(curry lsp)`

*v1.9.0-dev — 2026-07-20*

Language Server Protocol server for Curry Scheme. Any editor with an LSP
client — VS Code, Neovim, Vim (via `vim-lsp`/`coc.nvim`), Kate, KDevelop,
Qt Creator, Emacs (`eglot`/`lsp-mode`) — can drive it over stdio.

Implemented so far: real-time syntax diagnostics, hover text, and
completion. No go-to-definition or semantic (unbound-variable/arity)
diagnostics yet — see [Roadmap](#roadmap).

## Import

```scheme
(import (curry lsp))
```

Enabled by default (`-DBUILD_MODULE_LSP=ON`, no extra dependencies — it's a
plain `curry_c_module` like `f64vector` or `regex`).

---

## Scheme API

### `(lsp-serve)`

Start the stdio JSON-RPC transport. Blocks reading `Content-Length`-framed
messages from stdin until the client sends `exit`, then returns (0 if a
`shutdown` request preceded `exit`, 1 otherwise — matching the LSP spec's
recommended process exit code).

```scheme
(import (curry lsp))
(lsp-serve)
```

This is the entire Scheme surface for now — everything else is configured
editor-side by pointing an LSP client at `curry` as the server command:

```bash
curry -e '(import (curry lsp)) (lsp-serve)'
```

---

## What it does

### Diagnostics

On `textDocument/didOpen` and `textDocument/didChange`, the server runs the
*real* reader (`src/reader.c`) over the buffer text and publishes a
`textDocument/publishDiagnostics` notification. Currently reports the
**first** read error found (unbalanced parens, malformed `#s` sexagesimal
literals, bad cuneiform tokens, unterminated strings, etc.) — not multiple
errors per pass. An empty `diagnostics` array is sent once the buffer parses
cleanly.

The reported line is anchored on where the *failing top-level form began*
(the reader's `g_reader_last_line`), not wherever the reader's cursor ended
up when it gave up — so an unterminated `(define ...` reports at the
`define`, not at the last line of the file.

Diagnostics only catch **syntax** errors. Unbound variables, arity
mismatches, and other semantic issues are not yet checked (would require a
non-executing static pass through the evaluator's binding logic — planned,
not implemented).

### Hover

On `textDocument/hover`, the server extracts the identifier token under the
cursor and looks it up in a static table generated from the same C sources
that drive the editor syntax grammars
([`tools/gen-editor-syntax.py`](../../tools/gen-editor-syntax.py) →
`modules/lsp/symbols_gen.h`):

- **Special forms** (`define`, `lambda`, `let`, …) — from `src/symbol_list.h`
- **Builtin procedures** (`display`, `map`, `mpfr-gamma`, …) — every
  `DEF(...)`/`cond_def(...)` registration across `src/*.c`
- **Akkadian synonyms**, both transliterated (`iddin`) and cuneiform (`𒁹`) —
  hovering shows what canonical form it resolves to, e.g.
  *"**𒁹** — Akkadian synonym for `define` (special form)"*

Hover text is currently just `name — kind` (or the Akkadian-synonym variant
above) — no argument lists or prose docstrings yet.

Regenerate the table after adding a builtin or Akkadian synonym:

```bash
python3 tools/gen-editor-syntax.py
```

### Completion

On `textDocument/completion`, the server returns the whole candidate set —
every special form, builtin, and Akkadian synonym from the same generated
table hover uses, plus **local bindings** — every name introduced anywhere
in the current buffer by `define`, `define-syntax`, `lambda` (including a
dotted rest argument or a single-symbol formal list), `let`/`let*`/`letrec`/
`letrec*` (including named `let`), and `do`.

Local bindings are found by reading the buffer with the same reader
diagnostics uses and walking the resulting parsed forms structurally —
not by regexing the source text — so a `;` comment or a string literal
that happens to contain the word `define` can never produce a phantom
completion. Collection stops at the first syntax error in the buffer
(whatever was found before the error is still returned); it's capped at
256 distinct local names.

The server does not filter by the identifier prefix you're currently
typing — this is standard for a simple completion provider; the editor
filters the returned list against your in-progress token itself.

---

## Editor setup

Any LSP client pointed at the following command works. The exact
registration syntax is editor-specific; a few starting points:

**Neovim** (`nvim-lspconfig`-style, no upstream `lspconfig` entry yet — use
a manual `vim.lsp.start`):

```lua
vim.api.nvim_create_autocmd("FileType", {
  pattern = "curry",  -- see editors/vim/ftdetect/curry.vim
  callback = function()
    vim.lsp.start({
      name = "curry-lsp",
      cmd = { "curry", "-e", "(import (curry lsp)) (lsp-serve)" },
      root_dir = vim.fs.dirname(vim.fs.find({ ".git" }, { upward = true })[1]),
    })
  end,
})
```

**VS Code**: requires a small client extension wrapping
`vscode-languageclient` with `command: "curry"`,
`args: ["-e", "(import (curry lsp)) (lsp-serve)"]` — not yet packaged as
part of `editors/vscode/`; the existing VS Code integration
([`docs/reference/`](.) editors README) is syntax-highlighting only so far.

**Kate**: Settings → LSP Client → User Server Settings, add a `curry` entry
with the same command/args under `"command"`.

---

## Protocol notes

- **Transport**: stdio only, `Content-Length`-framed JSON-RPC 2.0 (this is
  LSP's own framing — distinct from the newline-delimited stdio the
  `(curry mcp)` module uses).
- **Sync mode**: full-document (`textDocumentSync: 1`) — every
  `didChange` notification is expected to carry the whole new buffer text,
  not incremental edits.
- **Message size cap**: 64 MB per message; larger `Content-Length` headers
  are rejected (the connection ends, matching malformed-client behaviour).
- **Open document cap**: 512 concurrently open documents; opening a 513th
  is logged to stderr and silently ignored until another is closed.
- **Nesting depth cap**: `src/reader.c`'s recursive-descent reader has no
  built-in recursion limit — fine for trusted script files, but this
  module feeds arbitrary editor-buffer text into it on every keystroke. A
  document nested past 1000 levels of parens/brackets is rejected with a
  diagnostic instead of being handed to the reader, rather than risking a
  native stack overflow. Real Curry source never comes close to this; only
  pathological or adversarial input does.
- Single-threaded, single-client — one editor session per `lsp-serve` call,
  matching how editors spawn language servers (one subprocess per project/
  window).

---

## Roadmap

Rough order, cheapest/most-reused-code first:

1. ~~Diagnostics from the reader~~ — done
2. ~~Hover from the generated builtin/special-form/Akkadian table~~ — done
3. ~~`textDocument/completion` — generated table + local bindings~~ — done
4. `textDocument/definition` — scan the buffer (and `import`ed files) for a
   matching top-level `define`
5. `textDocument/documentSymbol` — outline view from top-level `define*` forms
6. Real semantic diagnostics (unbound variable, arity mismatch) — needs a
   non-executing static pass through `src/eval.c`'s binding logic; the
   hardest item on this list

---

## See also

- [Editor syntax highlighting](../../editors/README.md) — Vim, Kate, VS Code
  grammars generated by the same `tools/gen-editor-syntax.py` script
- [`(curry mcp)`](module-mcp.md) — sibling stdio JSON-RPC server module,
  whose framing/parsing conventions this module follows
