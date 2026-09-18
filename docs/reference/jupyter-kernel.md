# Jupyter kernel

`curry_jupyter` is a Jupyter kernel for curry Scheme, built on
[xeus](https://github.com/jupyter-xeus/xeus)/xeus-zmq. It is **not** a
`(curry X)` module — unlike the modules under `modules/`, it doesn't get
`dlopen`'d into a running `curry` process. It's a separate standalone
executable that Jupyter launches directly, owning the kernel's ZMQ sockets
for the process's lifetime, and links `curry_core` itself.

## Getting started (first time setup)

`xeus`, `xeus-zmq`, `xtl`, `cppzmq`, and `nlohmann_json` — the libraries the
kernel is built on — are only published on **conda-forge**, not Homebrew.
So building and running this kernel needs a conda-forge-capable package
manager. **micromamba** is the tool used here: a small, fast, standalone
program that manages isolated software environments (folders with their
own set of installed packages, so they can't collide with Homebrew or
anything else on your system) and can install conda-forge packages into
them. If you've never touched conda/mamba/anaconda before, that's the
entire mental model you need: one command creates a named environment,
one command "activates" it (puts its programs on your `PATH` for the
current terminal), and everything you install goes into that isolated
folder.

**Step 1 — install micromamba and register it with your shell.**

```bash
brew install micromamba
micromamba shell init --shell bash --root-prefix ~/mamba
```

(Use `--shell zsh` instead of `--shell bash` if `echo $SHELL` says
`/bin/zsh` — that's the default on modern macOS.) That second command adds
a short block to your shell's startup file (`~/.bash_profile` or
`~/.zshrc`) that defines the `micromamba` command and sets `~/mamba` as
the one consistent place all your environments live. **This step is
required before `micromamba activate` will work at all** — skipping it is
the single most common thing that goes wrong here, and it's a one-time
setup, not something you repeat per project.

After running it, **open a new terminal tab** (or run `source
~/.bash_profile` / `source ~/.zshrc`) so the change takes effect.

**Step 2 — create one environment with everything this needs**, both the
C++ build dependencies and the Python-side Jupyter frontend:

```bash
micromamba create -n curry-jupyter -c conda-forge \
  xeus xeus-zmq xtl cppzmq nlohmann_json cmake \
  jupyterlab jupyter_console
```

This downloads everything into `~/mamba/envs/curry-jupyter` — it doesn't
touch Homebrew, your system Python, or anything outside that folder.

**Step 3 — activate it, then build the kernel against it:**

```bash
micromamba activate curry-jupyter
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_JUPYTER_KERNEL=ON \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX"
cmake --build build --target curry_jupyter
```

(`$CONDA_PREFIX` is set automatically once the environment is activated —
it points at `~/mamba/envs/curry-jupyter`, so this works regardless of
where `~/mamba` actually lives on your machine.) If `xeus`/`xeus-zmq`
aren't found at configure time — e.g. you skipped Step 3's `activate` —
`BUILD_JUPYTER_KERNEL` just prints a warning and skips the target rather
than failing the whole `cmake -B build` configure.

**Step 4 — register the kernel with Jupyter** (still inside the activated
environment):

```bash
tools/install-jupyter-kernel.sh build/curry_jupyter --user
```

**Step 5 — start Jupyter:**

```bash
jupyter lab                        # full browser-based notebook UI
# or, for a lighter terminal-only client:
jupyter console --kernel curry
```

In JupyterLab, pick "Curry Scheme" from the kernel list when creating a
new notebook.

### Every time after the first setup

You only repeat Steps 1–2 once, ever (per machine). Each new terminal
session, all you need is:

```bash
micromamba activate curry-jupyter
jupyter lab
```

If you rebuild `curry_jupyter` (e.g. after pulling new commits), redo
Step 3's two `cmake` commands — no need to redo Step 4, the kernelspec
just points at the binary's path and the binary gets replaced in place.

### Troubleshooting: "two different curry-jupyter environments"

If `micromamba env list` ever shows a `curry-jupyter` environment under
somewhere like `/opt/homebrew/Cellar/micromamba/.../envs/curry-jupyter`
*and* another one under `~/mamba/envs/curry-jupyter`, it means Step 1
(the shell init) hasn't actually taken effect in whatever terminal ran
the `create`/`install` command — micromamba falls back to using its own
install directory as the default environment location when it can't find
a configured root. The fix is Step 1 itself: run `micromamba shell init`
as above, open a **new** terminal, and confirm with `echo
$MAMBA_ROOT_PREFIX` that it prints `~/mamba` (or run `micromamba env
list` and check the `curry-jupyter` row's path) before creating or
installing anything else. Any stray environment under the Homebrew Cellar
path is safe to delete — `rm -rf` that specific `envs/curry-jupyter`
directory (never anything else under Cellar).

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

### Displaying images inline: `(jupyter-display-file path)`

Only available inside this kernel (registered straight into `GLOBAL_ENV`
in `configure_impl()`, `modules/jupyter/interpreter.cpp` — not compiled
into `curry_core`, so the plain `curry` REPL/CLI doesn't have it). Reads
`path` and publishes it as a Jupyter `display_data` message so it renders
inline in the notebook, instead of only existing as a file on disk. The
MIME type is inferred from the extension:

| Extension | MIME type | Encoding |
|---|---|---|
| `.png` | `image/png` | base64 |
| `.jpg` / `.jpeg` | `image/jpeg` | base64 |
| `.svg` | `image/svg+xml` | raw text (SVG is XML, not binary — base64-encoding it would be valid but pointless) |

Any other extension raises an error. Typical use, after `(curry plplot)`
writes a file:

```scheme
(import (curry plplot))
(plot-device "pngcairo")
(plot-output "plot.png")
(plot-init)
...
(plot-end)
(jupyter-display-file "plot.png")
```

See [`docs/reference/module-plplot.md`](module-plplot.md) for full plotting examples.

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
- **Rich display for a cell's own *result* value**: an `execute_result`
  (the value a cell evaluates to) still only ever publishes as
  `text/plain` — no `text/latex` for symbolic CAS values via `sym->latex`,
  for instance. File output (see below) already has a path to rich
  display; only in-process values don't yet.
