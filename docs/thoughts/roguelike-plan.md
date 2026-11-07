# Multi-player roguelike in curry — plan

*Drafted 2026-11-07. Status: planning only, nothing implemented yet.*

## What we are building

A roguelike written in curry, in the style of Rogue and Moria:

- Rogue's monsters (26, one per letter) and item types: potions, scrolls,
  rings, wands and staffs, armor, weapons, food, the amulet.
- Levels larger than the terminal, with a scrolling view, as Moria had.
- **Multi-player and co-operative**: one server holds the dungeon, several
  people connect and play in the same dungeon at the same time.
- Kept simple. The first version is the core game, not every idea below.

It is a game for fun, and a place to use curry's own features (actors,
sockets, Akkadian names, sexagesimal numerals, quantum values). It is not a
port of Rogue and will not play move for move like it.

## Decisions already made

| Topic | Decision | Reason |
|---|---|---|
| Content | Rogue's 26 monsters and item types | Known, small, balanced enough to start |
| Players | Several, co-operative, one shared dungeon | The reason for the project |
| Turns | Each action has a time cost; the game does not wait for everyone | A slow player must not stall the others |
| Idle players | A timeout passes their turn | Same reason |
| Map size | Larger than the screen, about 198×66, set by a setting | As in Moria |
| Scrolling | View follows the player (centred); panel-jump style may come later as a setting | Easier to follow |
| Display | Text over TCP; players use an ordinary terminal client | No client code to write |
| Randomness | `(srfi 27)`, seeded; the starting seed is taken from the OS | Replayable games from a seed |
| Network exposure | Local network only at first | Avoids the security work of an open server |

## Not decided yet

1. **Player classes.** Rogue has none; its descendants do. Plan: build the
   classless core first, then add a small set of classes. Candidate set
   drawn from curry's features: Scribe, Diviner (quantum effects),
   Astronomer, Algebraist. Needs the user's input before work starts.
2. **Player against player.** Not in the first version.
3. **Name** of the game and its directory. `examples/roguelike/` until chosen.
4. **Turn timeout length.** Start at 5 seconds, make it a setting.

## What curry already gives us (checked 2026-11-07)

- `(srfi 27)`: seeding works. Seeding twice with `(42 7)` gave the same
  three numbers both times.
- `(curry network)`: `tcp-listen`, `tcp-accept`, `tcp-connect`, `tcp-close`,
  `socket-ready?`, `socket-set-nonblocking!`, plus SRFI 106 sockets.
- Actors: `spawn`, `send!`, `receive`. Each actor is its own thread with a
  mailbox.
- `(curry ncurses)` (FFI to the system ncurses). **It draws to the local
  terminal only.** The server cannot use it to draw on a remote player's
  screen, so remote players get ANSI escape codes written by our own code.
  ncurses is useful only for an optional local single-player front end.
- `(curry sdl3)` (branch `feat/sdl3`, not merged): a possible later tile
  front end. Its only text is an 8×8 debug font.

## Not yet checked (do this in Phase 0)

- How many simultaneous connections `(curry network)` handles and whether
  `socket-ready?` is cheap enough to poll.
- Whether `nc` and `telnet` clients can both work. Telnet sends control
  bytes (the byte 255, "IAC") that must be removed from the input, and can
  report the window size. `nc` does neither.
- Where the default random source in `(srfi 27)` gets its seed.
- Cost of redrawing a 100×40 view as ANSI text for several players every
  turn.

## Architecture

```
 player terminals (nc / telnet)
        |  TCP, text
 +------+------------------------------+
 |  connection actors (one per player) |   read lines, strip telnet bytes,
 |  - parse input                      |   forward key events; write output
 +------+------------------------------+
        | mailbox messages
 +------+------------------------------+
 |  game loop (ONE thread)             |   owns ALL game state
 |  - turn scheduler (time-cost queue) |
 |  - level, monsters, items, players  |
 |  - renders each player's view       |
 +-------------------------------------+
```

**The one rule:** only the game loop reads or changes game state. Connection
actors never touch the dungeon; they only pass messages. This avoids data
races, which are the main danger of mixing actors with shared mutable state.

**Turn scheduler.** Every actor in the game (player or monster) has a
`next-time`. The loop takes the one with the smallest time, lets it act, and
adds the cost of its action. A fast monster acts more often than a slow one.
A player with no input when their time comes is passed after the timeout.

**Rendering.** For each player, build a rectangular view of the map centred
on that player, clipped to the terminal size (default 80×24 if unknown), and
send it as ANSI text. Send only what changed since the last frame if full
redraws prove too slow (measure first, do not guess).

**Randomness.** Separate random sources for level generation, combat and
cosmetic effects, so a fight cannot change the layout of the next level. A
game is replayable from its seed. The seed is shown to the players.

## Module layout (proposed)

```
examples/roguelike/
  main.scm        server entry point, option parsing
  data.scm        monster and item tables
  level.scm       level generator
  fov.scm         field of view
  sched.scm       time-cost scheduler
  combat.scm      attack and damage rules
  render.scm      view building and ANSI output
  net.scm         connection actors, telnet byte handling
  README.md
tests/roguelike_tests.scm
docs/reference/roguelike.md    written when Phase 3 lands
```

Pure game rules (everything except `net.scm` and `main.scm`) take no
sockets and no terminal, so they can be tested without a network.

## Phases

Each phase ends with passing tests, a commit, and a review by a fresh
agent (as CLAUDE.md requires). Nothing is merged without a pull request.

### Phase 0 — Checks (about half a day)
Answer the "not yet checked" list above with small throw-away scripts.
**Exit:** a short note in this file with the results, and changes to this
plan if any check fails.

### Phase 1 — Single-process core (3–5 days)
Data tables for the 26 monsters and the item types; level generator (rooms
and corridors on the large map); field of view; scheduler; movement,
combat, items, unidentified items, hunger, stairs; one local player, no
network.
**Exit:** `roguelike_tests.scm`, deterministic by seed: the same seed gives
the same level and the same fight outcomes; every level is fully connected
(stairs reachable from the start); no monster or item inside a wall.

### Phase 2 — Display (1–2 days)
Viewport and ANSI renderer; optional local ncurses front end for testing.
**Exit:** view stays correct at map edges and at small terminal sizes;
a test renders known maps to strings and compares.

### Phase 3 — Server (3–4 days)
`net.scm`: accept connections, one actor each, input parsing with telnet
bytes removed, join and leave, per-player view, death and rejoin as a new
character, idle timeout.
**Exit:** a test script that starts the server on a local port, connects
two clients, moves both, and checks that each sees the other. Disconnecting
a client mid-turn does not stop the game.

### Phase 4 — Co-operative play (2 days)
Shared messages, a shout command, markers for allies off screen, shared
stairs rule (decide: the level changes for a player when that player takes
the stairs; others stay).
**Exit:** two clients on different levels do not disturb each other.

### Phase 5 — Curry flavour (as wanted, each independent)
Akkadian and cuneiform names; sexagesimal labels for unidentified items;
the unexpected-word table; monsters as actors that signal each other;
quantum monsters and chests; surreal-number depth below the last level;
symbolic scrolls. Pick items one at a time.

### Phase 6 — Classes (2–3 days, after the user decides the set)

### Phase 7 — Special levels (optional)
Black-hole level using the horizon-viewer code; four-dimensional level
using the `maze4d` code.

Rough total for Phases 0–4: **2 to 3 weeks of work**, mostly game rules.
The estimate is uncertain until Phase 0 is done.

## Security (before any use beyond a trusted local network)

- Bind to 127.0.0.1 by default; listening on the local network is an
  explicit option.
- Cap line length, total connections, and input rate per connection.
- Never pass player input to `eval` or `read`; commands are a fixed table.
- Strip every telnet command sequence and every ANSI escape from player
  input. Player names are limited to printable ASCII, so a name cannot send
  escape codes to another player's terminal.
- No accounts and no stored passwords in the first version.
- An independent security review of `net.scm` is required before Phase 3
  is merged.

## Risks

| Risk | Effect | Answer |
|---|---|---|
| Actors plus shared state race | Corrupt game | One game-loop thread owns state (see Architecture) |
| Slow redraw for many players | Laggy play | Measure in Phase 0; send diffs if needed |
| Large map plus many monsters is slow in `define-library` code | Slow turns | Known ~3× slowdown of library code; measure before optimising |
| Unknown terminal size | Broken view | Default 80×24; accept telnet window size when sent |
| Scope growth from the ideas list | Never finished | Phase 5 items are optional and independent |

## Out of scope for now

Accounts and persistence, saving games, player against player, internet
play, graphics, sound, a client program of our own.
