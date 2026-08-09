# Module: (curry graphviz)

*unreleased*

A small mutable graph builder plus a DOT-source writer, and a `graph-render!` that renders straight to an image via Graphviz's own C libraries (`libgvc`/`libcgraph`), dlopen'd at runtime.

## Import

```scheme
(import (curry graphviz))
```

Importing this module never requires Graphviz to be installed — only `graph-render!` does; `graph->dot-string`/`graph-write-dot` and the whole builder API need nothing beyond this module. `graph-render!` dlopens `libgvc`/`libcgraph` lazily, on its own first call, and raises with install instructions if they can't be found:

```
macOS:           brew install graphviz
Debian/Ubuntu:   apt install libgraphviz-dev
Fedora/RHEL:     dnf install graphviz-devel
```

Requires `-DBUILD_FFI=ON` (the general FFI flag — no Graphviz-specific CMake flag).

## Building a graph

### `(make-graph name)` / `(make-graph name directed?)`

Creates a new graph. `directed?` defaults to `#t`; pass `#f` for an undirected graph (edges render with `--` instead of `->`).

### `(graph-add-node! g id)` / `(graph-add-node! g id attrs)`

Adds a node. `attrs` is an alist of `(key . value)` pairs, e.g. `'((color . "red") (shape . "box"))`.

### `(graph-add-edge! g from to)` / `(graph-add-edge! g from to attrs)`

Adds an edge from `from` to `to`.

### `(graph-add-subgraph! g name)` / `(graph-add-subgraph! g name cluster?)`

Adds and returns a nested subgraph — itself a graph that `graph-add-node!`/`graph-add-edge!`/`graph-add-subgraph!` all work on directly, for building nested clusters. `cluster?` defaults to `#t`: Graphviz only draws a visible border around a subgraph whose name starts with `cluster`, so a cluster subgraph's name is automatically prefixed with `cluster_` if it doesn't already start with that. Pass `#f` for a plain, border-less grouping subgraph instead.

### `(graph-set-attr! g key value)`

Sets a graph-level attribute (e.g. `(graph-set-attr! g 'rankdir "LR")`). Overwrites if `key` was already set.

### `(graph-set-node-defaults! g attrs)` / `(graph-set-edge-defaults! g attrs)`

Sets default attributes applied to every node/edge added *from this point onward* — Graphviz's own `node [...]`/`edge [...]` statement semantics; nodes/edges already added aren't retroactively affected.

## Values

Node/edge identifiers and attribute values are auto-quoted: any number is written bare, everything else (strings, symbols, anything else via `write`) is always double-quoted and escaped (`"`, `\`, and embedded newlines). This is simpler than detecting every case DOT's own bare-identifier grammar permits unquoted, and a quoted string is always valid DOT regardless of its content — including DOT's reserved keywords (`graph`, `node`, `edge`, ...), which would otherwise need special-casing to avoid emitting them unquoted where DOT requires an identifier.

```scheme
(import (curry graphviz))
(define g (make-graph 'G))
(graph-set-attr! g 'rankdir "LR")
(graph-add-node! g 'a '((label . "Alpha") (color . "red")))
(graph-add-node! g 'b)
(graph-add-edge! g 'a 'b '((label . "step 1")))
(graph->dot-string g)
; =>
; "digraph \"G\" {\n  rankdir=\"LR\";\n  \"a\" [label=\"Alpha\", color=\"red\"];\n  \"b\";\n  \"a\" -> \"b\" [label=\"step 1\"];\n}\n"
```

## Writing

### `(graph->dot-string g)` → string

Serializes `g` to a DOT source string.

### `(graph-write-dot g port)`

Writes `g` as DOT source directly to `port`.

## Rendering

### `(graph-render! g out-path)` / `(graph-render! g out-path format)` / `(graph-render! g out-path format engine)`

Renders `g` to `out-path`. `format` is any Graphviz output-format name (`"png"`, `"svg"`, `"pdf"`, ...; default `"png"`). `engine` is any Graphviz layout engine name (`"dot"`, `"neato"`, `"fdp"`, `"circo"`, `"twopi"`, `"sfdp"`, ...; default `"dot"`).

Parses this module's own generated DOT text via `agmemread` — so nothing here needs to reconstruct the graph through Graphviz's own node/edge construction API; `graph->dot-string` already does that job. Every native handle (the layout, the parsed graph, the rendering context) is freed before `graph-render!` returns, or before it raises, on any failure partway through.

```scheme
(import (curry graphviz))
(define g (make-graph 'G))
(graph-add-node! g 'a) (graph-add-node! g 'b)
(graph-add-edge! g 'a 'b)
(graph-render! g "graph.png")
(graph-render! g "graph.svg" "svg")
(graph-render! g "graph-circular.png" "png" "circo")
```

## Notes

- This module has no DOT *reader* — it only ever writes DOT it generated itself from `make-graph`'s builder API. There's no way to load an existing `.dot` file into a `graph` object.
- `graph-render!`'s dependency on Graphviz's C libraries is a genuinely separate concern from the builder/writer API above — see `(curry hdf5)`/`(curry ncurses)` for the same "dlopen a native library lazily, at first actual use, not at import time" pattern this module follows.

## See also

- [`module-hdf5.md`](module-hdf5.md), [`module-ncurses.md`](module-ncurses.md) — the other modules using this same runtime-dlopen FFI pattern
- [`module-ffi.md`](module-ffi.md) — the general C FFI this module's `graph-render!` is built on
