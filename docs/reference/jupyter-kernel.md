# Jupyter kernel

`curry_jupyter` is a Jupyter kernel for curry Scheme, built on
[xeus](https://github.com/jupyter-xeus/xeus)/xeus-zmq. It is **not** a
`(curry X)` module — unlike the modules under `modules/`, it doesn't get
`dlopen`'d into a running `curry` process. It's a separate standalone
executable that Jupyter launches directly, owning the kernel's ZMQ sockets
for the process's lifetime, and links `curry_core` itself.

## Building

`xeus`, `xeus-zmq`, `xtl`, `cppzmq`, and `nlohmann_json` are conda-forge
packages, not available via Homebrew. Get them via micromamba (or any other
conda-forge-capable tool):

```bash
brew install micromamba
micromamba create -n curry-jupyter -c conda-forge \
  xeus xeus-zmq xtl cppzmq nlohmann_json cmake

cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_JUPYTER_KERNEL=ON \
  -DCMAKE_PREFIX_PATH="$HOME/mamba/envs/curry-jupyter"
cmake --build build --target curry_jupyter
```

If `xeus`/`xeus-zmq` aren't found at configure time, `BUILD_JUPYTER_KERNEL`
just warns and skips the target rather than failing the whole configure.

## Installing the kernelspec

```bash
tools/install-jupyter-kernel.sh build/curry_jupyter --user
```

This requires the `jupyter` CLI (`kernelspec install`) — the easiest source
is the same conda env used to build:

```bash
micromamba install -n curry-jupyter -c conda-forge jupyter
```

Then `jupyter console --kernel curry` or `jupyter notebook` (selecting the
"Curry Scheme" kernel) will work.

## Execution model

Each cell's top-level forms run through the exact same path the REPL uses
per input — `compiler_compile()` then `vm_run()` (`src/main.c`), not the
tree-walker (`eval()`). One `curry_interpreter` instance, and therefore one
`GLOBAL_ENV`/VM, lives for the whole kernel process, so definitions persist
across cells the same way they persist across REPL inputs.

`stdout` is captured per top-level form (redirecting `current-output-port`
to a string port for the duration of that form) and republished as a
Jupyter `stream` message — output from a still-running form isn't visible
until that form returns, since curry has no port type that streams writes
incrementally.

A cell stops at its first raised condition; the reply's `evalue` carries
curry's own Akkadian-preamble error text. State from forms that already
ran in that cell (and any earlier cell) is preserved — an error doesn't
reset `GLOBAL_ENV`, only the VM's operand stack (`vm_reset()`).

### Interrupting a busy cell

`main.cpp` builds the kernel with `xeus::make_xserver_shell_main` — the
split server, which polls the control channel on its own thread separate
from the shell thread a busy cell blocks. An `interrupt_request` sets a
cross-thread atomic flag (`src/interrupt.c`) that `vm_run()`'s dispatch
loop checks at every instruction (the same per-instruction safepoint the
minor-GC poll and the interactive debugger's hook already use — see
`vm.c`'s `L_DISPATCH`); when set, it raises an `EC_INTERRUPTED` condition,
which unwinds through the cell's ordinary `SCM_PROTECT` exactly like any
other raised exception, surfacing as a normal error reply.

The kernelspec must set `"interrupt_mode": "message"` (which
`tools/install-jupyter-kernel.sh` does) — without it, `jupyter_client`'s
default is to send a raw `SIGINT` to the process instead of a
control-channel message, and `curry_jupyter` has no `SIGINT` handler: an
uncaught `SIGINT`'s default action terminates the whole process (verified
during development -- worse than a hang, and the reason this field is
required rather than optional).

The interrupt safepoint only exists in `vm.c`'s bytecode dispatch loop,
not in `eval()`'s tree-walker (`src/eval.c`) — the same structural gap
the interactive debugger already has (its own doc notes it's invisible
to tree-walked code). A cell whose hot loop happens to run through
`eval()` rather than compiled VM bytecode (e.g. an infinite loop inside
a `define-library` body) cannot currently be interrupted.

## Known limitations

- **`is_complete_request` mis-tracks state across lines within a cell**:
  it reuses `curry_line_depth()` (the same nesting tracker the REPL's
  `rl_read_expr` uses), summed independently per line. That function
  resets its "inside a string"/"inside an escape" state at the start of
  every call, so a paren that appears inside a string literal or a `#|
  ... |#` block comment spanning multiple lines gets miscounted as real
  nesting. This is an existing REPL limitation (readline input has the
  same problem, one line at a time), not something the kernel introduces
  -- but it's more likely to bite in a notebook cell, where multi-line
  strings/comments are common. A real fix needs a full-buffer tracker
  that carries string/comment state across the whole cell, not per line.

## Known gaps (not yet implemented)

- **Completion / inspect (hover)**: `complete_request`/`inspect_request`
  currently return empty results. The `(curry lsp)` module already
  implements the same builtin-table-plus-local-binding-collection logic
  this would need, but it's compiled into a `dlopen`'d `.so` that expects
  a live curry process to resolve `curry_core` symbols from — it isn't
  linkable into this standalone binary as-is. Reusing it means lifting
  that logic out of `modules/lsp/lsp.c` into something both can link
  against; out of scope for the initial kernel.
- **Rich display**: results only ever publish as `text/plain`. Symbolic
  CAS values could route through `sym->latex` as `text/latex`, and
  `qt6`/`plplot` canvases could snapshot to `image/png` — neither is
  wired up yet.
