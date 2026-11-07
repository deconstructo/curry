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

1. **What the character choices do in the rules.** The lists are decided
   (see "Characters" below). What each choice changes is proposed there and
   needs the user's yes or changes.
2. **Player against player.** Not in the first version.
3. **Name** of the game and its directory. `examples/roguelike/` until chosen.
4. **Turn timeout length.** Start at 5 seconds, make it a setting.

## Characters

Rogue has no classes; this game does. A new player chooses four things.
The lists below are the user's. Rogue had none of them.

| Choice | Options |
|---|---|
| Class | Valkyrie, Fighter, Cleric, Thief, Wizard, Bard, Bon vivant |
| Gender | Male, Female, Hermaphrodite, Spicy |
| Race | Human, Sióg |
| Sexuality | Asexual, Celibate, Ambivalent, Multivalent, Univalent |

(Spelling normalised from the user's message: "valkrie" is Valkyrie and
"mutivalent" is Multivalent. Confirm.)

### Proposed rules (not yet agreed)

Principle: keep the first version simple. Class and race change the
numbers. Gender changes the words. Sexuality changes how a character acts
with other players and with charming monsters, which is where the
multi-player game gets its own flavour.

**Classes** (starting kit and one ability each):

| Class | Starting kit | Ability |
|---|---|---|
| Valkyrie | Spear, light armor | Can drag a dying ally to the stairs |
| Fighter | Sword, heavy armor | Highest health, extra damage |
| Cleric | Mace, healing potion | Heals adjacent allies; resists undead drain |
| Thief | Dagger, lockpicks | Finds traps; steals from monsters; moves quietly |
| Wizard | Staff, one scroll | Identifies items by use; low health |
| Bard | Lute, food | Calms monsters; each turn gives nearby allies a small bonus |
| Bon vivant | Wine, good food, no weapon | Eating and drinking give extra benefit; smooth talk can make monsters hesitate |

**Races:**

| Race | Effect |
|---|---|
| Human | Plain; no special bonus or weakness |
| Sióg | Sees further in the dark; light and quick; hurt more by iron weapons and armor |

**Gender:** sets the pronouns used in game messages and the character
sheet. Each option has its own word list, kept in one table the user can
edit; the user chooses the words for Hermaphrodite and Spicy. No effect on
stats.

**Sexuality** (proposal: bonuses and resistances only, nothing that blocks
play):

| Choice | Effect |
|---|---|
| Asexual | Cannot be charmed by monsters |
| Celibate | Small bonus to resisting temptation effects and to sticking to a vow (searching, resting) |
| Ambivalent | Re-rolled each level: a small bonus or penalty, chosen at random |
| Multivalent | Bonus that grows with the number of allies next to the character |
| Univalent | Larger bonus while exactly one ally is next to the character; none otherwise |

All of these need a "charm" effect to exist: some monsters and scrolls
attempt to charm. If the user does not want charm in the game, Asexual and
Celibate lose most of their meaning and would need a different effect.

### Technical notes

- A character is one record: class, gender, race, sexuality, plus stats.
  Each choice is a row in a table, so adding an option later is a data
  change, not a rule change.
- With 7 × 4 × 2 × 5 = 280 combinations, tests cover every class and race
  and each gender and sexuality separately, not every combination.
- Names for all four fields are shown on the join screen and the character
  sheet. Free-text fields are not allowed (see Security).

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

### Phase 6 — Characters (3–4 days, after the user agrees the rules above)
Class, race, gender and sexuality tables; the join screen; the charm effect;
one test per class and race, and per gender and sexuality.

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
