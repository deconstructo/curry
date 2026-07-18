# Editor support for Curry Scheme

Syntax highlighting for Vim/Neovim, Kate (and every KSyntaxHighlighting
consumer: KWrite, KDevelop, Qt Creator), and VS Code.

All three grammars highlight the full language: R7RS special forms, all
core builtins, the Akkadian transliterated and cuneiform synonyms,
Neugebauer sexagesimal literals (`#s1;30`), and cuneiform numerals
(`𒌋𒁹`) — with the reader's own priority rule that a cuneiform synonym
beats a numeral reading (`𒁹` is `define`, not `1`).

The grammar files are **generated** from the C sources by
`tools/gen-editor-syntax.py`, which parses `src/symbol_list.h`,
`src/akkadian_names.h`, and every `DEF(...)`/`cond_def(...)` registration
in `src/*.c`. After adding a builtin or synonym, regenerate:

```bash
python3 tools/gen-editor-syntax.py
```

Open `editors/sample.scm` in the editor to check the result.

## Vim / Neovim

Copy (or symlink) the three directories into your runtime path:

```bash
# Vim
cp -r editors/vim/{syntax,ftdetect,ftplugin} ~/.vim/

# Neovim
cp -r editors/vim/{syntax,ftdetect,ftplugin} ~/.config/nvim/
```

By default `.scm`/`.sld` files keep Vim's builtin scheme filetype; the
curry filetype activates when the file's shebang mentions curry. To use
it for all `.scm`/`.sld` files, add to your vimrc:

```vim
let g:filetype_scm = "curry"
```

## Kate / KWrite / KDevelop / Qt Creator

```bash
mkdir -p ~/.local/share/org.kde.syntax-highlighting/syntax
cp editors/kate/curry.xml ~/.local/share/org.kde.syntax-highlighting/syntax/
```

(macOS Qt Creator: `~/Library/Application Support/org.kde.syntax-highlighting/syntax/`.)

The definition registers for `*.scm;*.sld` with a priority above the
bundled Scheme highlighter, so it wins automatically.

## VS Code

Install as a local extension by copying the folder:

```bash
# Linux/macOS
cp -r editors/vscode ~/.vscode/extensions/curry-lang.curry-scheme-0.1.0
```

Then reload VS Code. The extension claims `.scm`/`.sld`; if you also work
with other Schemes, use `"files.associations"` per-workspace instead.

To build a distributable `.vsix`: `cd editors/vscode && npx vsce package`.
