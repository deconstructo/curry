# Notes for The Anarchist's Curry Cookbook

## What the book is

A practical guide to using Curry for ethical, intellectual anarchism — to explore, to build,
to play useful jokes. Not chaos; the refusal of bureaucracy. Languages that make you think
like a compiler instead of like a mathematician are the enemy.

---

## Opening chapter concept

**Epigraph:** John 1:1 — "In the beginning was the Word."

Then Genesis: God creates through "let there be X" — which is literally a `let` binding.
God is evaluating expressions into existence. The universe is a REPL.

**The argument:** John's *Logos* (Word/Reason) maps onto Lisp's core insight: code and data
are the same thing. The Word *is* the world; the program *is* its output. Homoiconicity as
theology.

Genesis proceeds by naming — "and God called the light Day" — which is just `define`.
Creation is binding symbols to values in the global environment.

"And God saw that it was good" — that's the REPL printing the return value.

**Punchline:** "But in what language was the Word? Arguably, if God knows mathematics and
programming, the Word was in Lisp."

### The Curry second layer

The oldest written language (Sumerian cuneiform) turns out to be valid syntax in a Lisp.
The clay tablet *is* the source code. The scribe at Ur and the programmer at a terminal are
doing the same thing — inscribing symbols that carry force.

See `examples/fibonacci-akkadian.scm` — an iterative Fibonacci in Standard Babylonian
Akkadian, written entirely in cuneiform. It works. It's absurd. It makes a point.

The opener earns its place because the book is arguing something real: Lisp/Curry is a
*truer* notation — closer to thought than to machine. Using it is a kind of reclamation.
Lisp before Turing. Lambda before the machine. The Word before the hardware.

---

## Draft opening

> *In the beginning was the Word.*
> — John 1:1

God, as every theologian knows, is a programmer. The evidence is in Genesis. He does not
build the world with his hands — he *speaks* it into existence. "Let there be light." Let
there be a firmament. Let the waters be gathered. Each act of creation is an expression
evaluated against the void. Each naming — "and God called the light Day, and the darkness
he called Night" — is a binding, a symbol assigned a value in the global environment.

And when he steps back and surveys what he has made and pronounces it good, that is the
REPL printing the return value.

This raises an obvious question: in what language was the Word?

Arguably — if God knows mathematics and programming, and we must assume he does — the Word
was in Lisp. Nothing else has the right shape. Lisp is the language where code and data are
the same thing, where a program and its output share a single substance. The Word *is* the
world. The expression *is* what it evaluates to. John would have recognised this immediately.
He just lacked the vocabulary.

We have the vocabulary. And we have Curry.

Curry is a Lisp. But it is also, if you look at it sideways, an ancient Babylonian tablet.
The cuneiform characters your eye might mistake for decoration are valid syntax. The Sumerian
scribe pressing wedge-marks into clay and the programmer pressing keys in a terminal are
doing the same thing: inscribing symbols that carry force, that *do* something when read by
the right interpreter.

This book is about using that force well. Not destructively — that is the other cookbook.
Ethically. Intellectually. With occasional useful absurdity. The anarchism here is not
chaos; it is the discipline of following an argument wherever it leads, regardless of
whether the destination looks respectable from the outside.

Developers have spent decades asking what a pure language would look like — one that gets
out of the way of thought, that makes the structure of a program identical to the structure
of an idea. The answer the field keeps arriving at, and keeps flinching away from, is Lisp.
Everything else is a compromise with the machine, or with the committee, or with the people
who find parentheses unsettling.

Writing production code in Akkadian cuneiform is not a joke. It is the logical conclusion
of taking the question seriously. The notation is arbitrary. The structure is not. And if
you have a language where the oldest writing system on earth is valid syntax, you have
demonstrated something true about what language is — not by arguing it, but by running it.

But Akkadian is only the beginning of that argument. Because the same mechanism that makes
cuneiform valid Curry can make any language valid Curry — ancient Hebrew, Classical Greek,
Old Irish, Old English, and, critically, the languages that are still alive but only just.
The threatened ones.

There is a paper — whose conclusions are impossible to unread once you have read them —
about the absence of women from programming language design, and what that absence costs.
Languages are not neutral. They encode the assumptions of the people who build them:
what counts as a natural metaphor, what gets a keyword and what gets a library call, what
feels elegant and what feels ugly. The history of programming languages is largely a history
of a particular kind of mind designing tools in its own image and then wondering why other
kinds of minds find them unwelcoming.

Curry's pluggable language support is a structural answer to that observation. No surface
language is privileged. The evaluator does not care whether the keyword for "define" is
typed in ASCII or pressed into clay or written in a script with four living native speakers.
The Word is available in all languages, or it is not really the Word.

Consider the praxis of this. An artist — a speaker of Welsh, or Māori, or Scots Gaelic, or
Cherokee — wants to make a piece of digital art. Not art *about* their language, but art
*in* it: code that runs, a program that does something in the world, written in the language
their grandmother spoke. With pluggable language support, that is not a research project or
a grant proposal. It is an afternoon's work. The language stops being a specimen under glass
and becomes a tool again. The word becomes executable.

That is what the ethics is actually about. Not the senior engineers. Not the parentheses.
The question of who gets to speak, and in what tongue, and whether the machine will listen.

---

## The three pillars of anarchist Curry

**Exploration** — the numeric tower goes from fixnums to surreals to Clifford algebras to
symbolic CAS. Ask "what happens if I multiply a quaternion by a symbolic variable?" and it
just works. That's intellectual anarchism: refusing to accept that the answer should be
an error.

**Building** — the MCP server module lets you wrap any Curry computation as a tool for
Claude. Give an AI access to a symbolic differentiator, or an N-body simulator, written
in cuneiform. Or in Welsh.

**Praxis** — making a threatened language executable is not preservation, it is
*revitalisation*. The language lives not in an archive but in a running program, in a
gallery, in a thing that does something. An artist can write digital art in their
grandmother's tongue. The notation is arbitrary; the force is real.

---

## The profiling chapter epigraph

> *And the Scath said "let there be profiling, and let it support multiple levels, including
> hooking into eval.c, and let there be an extensible set of consumer interfaces including
> MCP, HTTP/S, REPL and MQTT (if enabled)"*

(The Scath speaks in the same register as Genesis — not because it is funny, but because
the form is right. A fiat. A binding. An expression evaluated against the void. And the
Scath saw that it was good.)

---

## Runtime profiling architecture

Inspired by the JVM's three-layer approach:

- **JVMTI** (C-level event hooks) → `eval.c` hooks behind a C global mirroring `**eval-profiler**`
- **JMX** (named interface over live data) → `(profiler-report)` returning structured data
- **JFR** (Java Flight Recorder) → per-thread lock-free ring buffer, ~1-2% overhead, drain on demand

### Levels (controlled by `(set! **eval-profiler** N)`)

- `0` — off, single not-taken branch, branch-predictor friendly, effectively zero cost
- `1` — named function call counts
- `2` — wall-clock timing per call (`clock_gettime`)
- `3` — full tracing: tail calls, anonymous lambdas, primitive dispatch, GC hooks

Same pattern for `**gc-profiler**` — separate knob, same mechanism.

### Pluggable transports

The profiling module collects and structures data. What consumes it is a separate concern:

```
eval.c hooks → (curry profiling)  →  (profiler-report) → alist
                                            ↓
                               ┌────────────┼────────────┐
                              MCP          HTTP         REPL
                         (curry           (curry      (display)
                       profiling-mcp)  profiling-http)
```

Each transport is a thin Scheme wrapper — ~30-40 lines — importing `(curry profiling)` and
the relevant transport module. The C layer doesn't change.

- **MCP** — use in Claude Code / any MCP client; tools: `start-profiling`, `stop-profiling`, `get-profile`
- **HTTP/SSE** — use with Grafana or any browser dashboard; extends the existing SSE transport
- **MQTT** — use on constrained devices or where MQTT infrastructure already exists
- **REPL** — `,profile` command for interactive use

The same pattern as `mcp_math.scm`: import, register handlers, pick a transport. The
profiling story is identical.

---

## Influences / debts to acknowledge

- The paper on women and programming language design (find citation) — the argument that
  languages encode the assumptions of their designers, and that a monoculture of designers
  produces languages hostile to everyone else.

---

## Structure

Cookbook format — recipes, ingredients, techniques. Short worked examples that demonstrate
the philosophy through the practice (like the Akkadian Fibonacci).

Possible chapter domains:
- Symbolic math / CAS
- Actors and concurrency
- MCP tools (giving AI a Curry brain)
- Graphics (qt6 / gfx layer)
- The numeric tower as a playground
- Writing in your own language (pluggable language support)
- Threatened languages and digital art
