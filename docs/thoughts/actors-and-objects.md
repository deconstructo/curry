# The Same Idea, Twice
## Actors and Objects: A History of One Concept Diverging and Returning

*Background reading for Curry's object system design.*

---

## 1973: Two People, One Idea

In 1973, two things happened at MIT that looked unrelated at the time.

Carl Hewitt, Barbara Baker, and Carl Manning published *"A Universal Modular ACTOR
Formalism for Artificial Intelligence."* An **actor** is a computational entity that:

- Has a unique identity (an address)
- Communicates exclusively by sending messages
- Processes one message at a time
- In response to a message, can: send messages to other actors, create new actors,
  designate how to handle the *next* message

That's it. No shared memory. No global state. Computation is message passing between
autonomous entities.

The same year, Alan Kay — working on Smalltalk at Xerox PARC — was developing what he
called "object-oriented programming." An **object** is a computational entity that:

- Has a unique identity
- Communicates exclusively by sending messages
- Encapsulates state that only it can modify
- Responds to messages with behavior

Read those two definitions again. They are the same definition.

Kay has said this explicitly, repeatedly, for decades. His inspiration was biology —
cells that communicate by chemical signals, never by reaching directly into each other.
The object was supposed to be a cell. The message was the signal. The class hierarchy
was supposed to be loose, not the rigid taxonomies that C++ and Java later built.

He has also said, more than once, that he regrets the name "object-oriented." He should
have called it "message-oriented." The word "object" sent everyone looking at the nouns
— the things — instead of the verbs — the messages. That mistake propagated for fifty
years.

---

## The Divergence: How OOP Lost Its Way

Between 1973 and 1995, something went wrong.

C++ arrived in 1983. It kept the word "object" and the word "class" but silently dropped
the most important constraint: message passing. In C++, a method call is not a message —
it is a direct function call with a hidden `this` pointer. The caller and the object
share an address space. There is nothing preventing you from reading another object's
private memory if you are willing to cast a pointer.

Java in 1995 was cleaner but made the same choice. Python, Ruby, C# — all the same.
The method call desugared to a function call. Objects became structs with functions
attached. The deep idea — autonomous entities, no shared state, communication only by
message — was quietly discarded in favor of something that was easier to compile and
faster to run.

What got built instead was **encapsulation by convention.** Private fields are private
because the compiler enforces a visibility rule, not because the memory is actually
inaccessible. And in most languages, reflection or unsafe code can bypass that rule
entirely. The encapsulation is a polite suggestion.

The result: mutable shared state, race conditions, data corruption under concurrency,
the entire class of bugs that comes from two threads touching the same object. None of
this was possible in the original model. You cannot have a race condition on a message
queue.

---

## 1986: Erlang Rediscovers the Idea

Joe Armstrong, Robert Virding, and Mike Williams at Ericsson were trying to write
software for telephone switches. Telephone switches need to:

- Handle thousands of simultaneous calls
- Never go down (four nines or better of uptime)
- Recover from failures in parts without crashing the whole

They built Erlang. It has processes (not threads — no shared memory between them),
message passing, and let-it-crash fault tolerance via supervision trees. A failing
process is isolated; its supervisor restarts it; the rest of the system continues.

They did not know they were reinventing the Actor model. They arrived at the same place
from an engineering problem, not from a theory. Armstrong later said he realized the
connection after the fact.

OTP (Open Telecom Platform) built on Erlang introduced `gen_server` — a behaviour
(Erlang's word for a protocol or interface) that wraps a process in a request/reply
pattern. A `gen_server` has state, handles calls (synchronous) and casts (async), and
can be supervised. It is, to a first approximation, an object — but one whose
encapsulation is physical, not conventional.

The Erlang/OTP ecosystem built reliable distributed systems on this model for twenty
years before "microservices" became a marketing term for roughly the same architecture
running in Docker containers.

---

## 2007–present: The Reunion

As multicore processors made concurrency unavoidable, the industry rediscovered that
shared mutable state is the problem. This produced:

- **Clojure** (2007): persistent data structures and STM (software transactional memory)
  as an alternative to mutability
- **Go** (2009): goroutines and channels — lightweight actors by another name
- **Scala/Akka** (2009): actors on the JVM, explicitly Erlang-inspired
- **Elixir** (2011): Erlang's actor model with better syntax and tooling
- **Rust** (2015): outlaws shared mutable state through the ownership system rather than
  message passing, but arrives at the same safety guarantee by a different route

The trend: the industry is converging back on what Hewitt and Kay independently
described in 1973. Autonomous entities, no shared state, messages.

---

## What This Means for Curry

Curry's actor system (`spawn`, `send!`, `receive`) is already the correct foundation.
The object system being designed is not adding a new concept — it is giving the existing
concept a more convenient interface and a dispatch mechanism.

The key design decisions follow directly from taking the history seriously:

### Encapsulation is physical

There is no `slot-value` that can reach into an arbitrary object from outside. Not
"private by convention" — physically inaccessible because there is no shared memory
path. This is not a restriction for its own sake. It is the property that makes
concurrency correct by construction.

### Method calls are messages

When you call `(distance p1 p2)`, a message is sent to the `distance` generic function
actor, which then sends a message to `p1`'s actor. The synchronous appearance is sugar.
Underneath, it is always messages. This means:

- Every object call is already safe across threads — you never need to add a lock
- Every object is independently scheduled — a slow object does not block its caller
  unless the caller chooses to wait
- Distribution is a matter of degree — sending a message to a remote actor looks
  identical to sending to a local one (with different latency)

### The generic function is an actor too

In standard CLOS, a generic function is a special kind of object but not one that runs
concurrently. In Curry, the generic function is a full actor with its own mailbox. This
means:

- Adding a method at runtime is a message send — `define-method` at the top level
  compiles to `(send! distance :add-method! ...)` 
- Multiple threads can invoke the same generic function concurrently — the GF actor
  serializes dispatch decisions, which are cheap, and then each method runs in its own
  receiver actor
- The GF actor can be replaced, wrapped, or supervised like any other actor

### Objects and raw actors interoperate

A raw actor (using the existing `spawn`/`send!`/`receive` API) can receive messages from
object-actors and vice versa. They share the same mailbox protocol. This means you can
use a raw actor as a service (a cache, a connection pool, a logger) and call it from
method bodies without any conversion layer.

---

## The Message/Method Duality

The cleanest way to see the unification:

| Concept | Actor model | Object model | Curry |
|---------|-------------|--------------|-------|
| Identity | actor address | object reference | same thing |
| Communication | `send!` | method call | method call desugars to `send!` |
| State | captured in closure | slots | slots in actor closure |
| Behavior | `receive` patterns | method table | GF actor's method table |
| Creation | `spawn` | `make-instance` | `make-instance` calls `spawn` |
| Concurrency | natural | bolted on | natural |
| Encapsulation | physical | conventional | physical |

The object system is not a layer on top of the actor system. It is the actor system
with better naming, a dispatch mechanism, and a way to describe families of related
actors (classes).

---

## On `self`

In Smalltalk, `self` is the receiver of the current message. In most OOP languages,
`self` or `this` is a reference to the current object.

In Curry's actor-object model, `self` already means "the current actor" (from the
existing actor API). Inside a method body, it means the same thing — the actor that is
currently running this method. The method has direct access to its own slots (no message
needed) and uses messages to communicate with all other actors, including other objects.

This is the natural reading. `self` is not a special keyword bolted onto a function
call. It is the actor's own identity, which it has always had.

---

## The Smalltalk That Never Was

Kay has described, in various interviews, a version of Smalltalk that was more radical
than what shipped — where the message was truly first class, where there was no
distinction between local and remote objects, where the system was a network of
communicating entities all the way down.

What shipped was more conservative, for performance reasons and because the hardware of
the 1970s could not make the full vision practical.

Curry does not have those constraints. The full vision is implementable now. The
decision to make every object an actor, without exception, is the decision to build
the Smalltalk that never was.

---

## Further Reading

- Hewitt, Baker, Manning (1973) — *A Universal Modular ACTOR Formalism for Artificial
  Intelligence.* The original paper.
- Kay (1993) — *The Early History of Smalltalk.* Kay's own account; the regret about
  "object-oriented" is in here.
- Armstrong (2003) — *Making reliable distributed systems in the presence of software
  errors.* Armstrong's PhD thesis; the Erlang philosophy explained by its creator.
- Agha (1986) — *ACTORS: A Model of Concurrent Computation in Distributed Systems.*
  The canonical academic treatment of the Actor model.
- Lieberman (1986) — *Using Prototypical Objects to Implement Shared Behavior in
  Object-Oriented Systems.* The prototype-based alternative; worth understanding why
  we chose not to go this way.
