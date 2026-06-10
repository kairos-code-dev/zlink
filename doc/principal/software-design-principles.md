# Software Design Principles for Agentic Coding

> Based on *A Philosophy of Software Design* (John Ousterhout, 2nd ed.)  
> Distilled as actionable rules for AI agents generating, reviewing, and modifying code.

---

## Core Premise

**The fundamental problem in software development is complexity.**  
Complexity makes systems hard to understand, hard to change, and full of bugs.  
Every design decision must be evaluated by whether it reduces or increases complexity.

Complexity has three symptoms:

| Symptom | Description |
|---|---|
| **Change amplification** | A simple change requires modifications in many places |
| **Cognitive load** | You must know too much to understand or use a module |
| **Unknown unknowns** | You don't even know what you need to know — the most dangerous |

Complexity has two root causes:

- **Dependencies** — code cannot be understood or changed independently
- **Obscurity** — important information is not obvious

Complexity grows incrementally — not one big cause but hundreds of small additions.
**Sweat the small stuff.**

---

## Mindset: Strategic, Not Tactical

### The tactical trap

Tactical programming: "get this working as fast as possible."  
Each task adds a little complexity. Over months, the codebase becomes unmaintainable.  
Tactical coders look fast but leave damage behind.  
The "tactical tornado" — the fast, prolific developer — is a hero to management and a nightmare to teammates.

### The strategic investment

Strategic programming: "make the system easy to extend."  
Invest **10–20% of development time** in design.  
Payoff arrives in 6–18 months: faster development than the tactical path permanently.

**Working code is not the goal. Great design that also works is the goal.**

### For agents

- Do not take the first implementation that comes to mind. Consider at least two designs.
- Do not introduce complexity to meet a deadline. The cost compounds — you will pay more later.
- When modifying existing code, leave it slightly better. Fix nearby design problems encountered during the task.
- When tempted to work around a problem, fix its root cause instead.
- Never bypass a safety check or abstraction as a shortcut.

---

## Deep Modules

**This is the most important concept in the book.**

A module's **cost** = its interface (what others must learn and depend on).  
A module's **benefit** = its implementation (what it does).  
**Maximize benefit-to-cost: simple interface, powerful implementation.**

```text
+----------------------+  +----------------------+
| Deep Module (Good)   |  | Shallow Module (Bad) |
+----------------------+  +----------------------+
| simple API           |  | complex API          |
+----------------------+  +----------------------+
| large, rich          |  | thin implementation  |
| implementation       |  |                      |
+----------------------+  +----------------------+
```

### Canonical examples

| Deep (Good) | Shallow (Bad) |
|---|---|
| Unix `open/read/write/close/lseek` — 5 calls handle all I/O | Java `FileInputStream→BufferedInputStream→ObjectInputStream` |
| Garbage collector — zero interface, manages memory silently | Getter and setter for every field |
| TCP — hides packet loss, retransmission, ordering | A pass-through wrapper that adds nothing |

### Classitis

The mistaken belief that "more classes = better design" leads to **classitis**: many small, shallow classes.  
Each class is individually simple but the accumulated interface complexity is enormous.  
**Small is not a virtue. Deep is a virtue.**

### Rules

- Prefer fewer, more capable APIs over many small ones.
- If the interface is as complex as the implementation, the abstraction adds no value — it is shallow.
- **Simple interface > simple implementation.** Absorb complexity internally; give callers a clean surface.
- Default behavior should handle the common case without any configuration.

---

## Information Hiding

Each module must **encapsulate one design decision** — hiding it completely from callers.

What to hide: data structures, algorithms, protocols, file formats, external APIs, configuration choices.

### Information leakage — the most common design error

When a design decision is reflected in multiple modules, it has leaked.  
**Symptom:** changing one thing requires changes in many places.

Two forms of leakage:
- **Interface leakage** — a design decision appears in the public API
- **Back-door leakage** — two modules both know a fact without exposing it in their interfaces (worse, because it is invisible)

### Temporal decomposition — the most common trap

Structuring code by execution order rather than by information ownership:

```
// Wrong: Reader, Parser, and Writer all know the file format
Reader → Parser → Writer

// Right: one class owns all file format knowledge
FileHandler { read(), parse(), write() }
```

Do not split classes by "when things happen."  
Split by "what information each piece owns."

### `private` ≠ information hiding

Declaring a field `private` and then providing a getter and setter exposes everything about it.  
The field's type, semantics, and usage are all visible through the accessor API.  
True information hiding means the field's **existence** is irrelevant to callers.

### Rules

- If two modules share the same piece of knowledge, merge them or extract the knowledge into one module.
- Each class should own exactly one coherent piece of design information.
- Callers must not need to know how a module works — only what it guarantees.
- Never expose internal representation in the public API.
- Do not add a getter and setter for every field by default — this is the opposite of information hiding.
- **The best feature is one the caller doesn't even know exists** (e.g., automatic buffering). Aim for "the module just does the right thing."

---

## General-Purpose Modules

**Over-specialization is the single largest source of software complexity.**  
General-purpose code is simpler, cleaner, and easier to understand than special-purpose code.

### The right level: "somewhat general-purpose"

The implementation serves today's needs.  
The **interface** is general enough to serve multiple uses.  
Do not tie the interface to the specific use case you happen to have today.

### Example: the wrong way

```java
// Tied to the specific UI — backspace, delete, deleteSelection
// These pollute the text class with UI concepts
void backspace(Cursor cursor);
void delete(Cursor cursor);
void deleteSelection(Selection selection);
```

### Example: the right way

```java
// General: operates on any range of text
void insert(Position position, String newText);
void delete(Position start, Position end);
Position changePosition(Position position, int numChars);
```

The general interface has *fewer* methods and is *simpler* to use.  
Special-purpose logic (which char does backspace delete?) moves up to the UI layer where it belongs.

### 3 questions to evaluate generality

1. **What is the simplest interface that satisfies all current needs?**  
   Fewer methods (without losing capability) = more general.

2. **How many different situations will this method be used in?**  
   If only one, it is probably too special-purpose.

3. **Is this API easy to use for the current need?**  
   If it requires significant extra code, it may be too general (not enough function).

### Push specialization upward (or downward)

- Special-purpose code belongs in the highest layer that needs it (e.g., UI-specific logic stays in the UI).
- Or push it downward into device drivers / adapters that implement a general interface.
- Never let special-purpose concerns leak into a lower-level general-purpose module.

### Remove special cases from code

A special case is an `if` statement whose condition exists only because of a design choice.  
Often, redefining a concept eliminates the special case:

- Instead of: "selection may be null, check before using"  
  Use: "selection is always present; when nothing is selected, it is an empty range (start == end)"

Special-case-free code is simpler, clearer, and has fewer bugs.

### Rules

- When writing a new module, make the interface more general than the current need dictates.
- If you have three special-purpose methods that do similar things, look for one general method that handles all three.
- Test generality: can the interface serve a second use case without modification?
- Push special-purpose logic to the highest layer that needs it.
- Eliminate special cases from code by redefining concepts so the general path handles the edge.

---

## Pull Complexity Downward

When unavoidable complexity exists, the module developer should absorb it — not the caller.

> "100 callers' pain is worse than 1 developer's extra effort."

### Config parameters as a trap

Exposing a configuration parameter is often a way to avoid solving the hard problem.  
Before exposing a parameter, prove that the caller has information the module cannot access.  
If the module can observe and compute the right value (e.g., measuring round-trip time to set retry interval), do it — don't ask the user to configure it.

### Rules

- Provide sensible defaults. The common case must work without configuration.
- If a caller must handle a special case that the module could handle, pull it down.
- Do not throw an exception for something the module could silently handle correctly.
- Do not require callers to perform pre-steps or post-steps that belong inside the module.
- Each module must solve its problem completely. Configuration parameters signal an incomplete solution.

---

## Different Layers, Different Abstractions

Adjacent layers must not have the same abstraction. If they do, one layer is unnecessary.

### Pass-through methods

A pass-through method does nothing except call another method with the same signature:

```java
// TextDocument.insertString just calls TextArea.insertString — adds nothing
public void insertString(String text, int offset) {
    textArea.insertString(text, offset);
}
```

This is a symptom of unclear responsibility boundaries between classes.

**Resolution options:**
1. Expose the lower class directly to callers (eliminate the upper class)
2. Redistribute functionality between the classes
3. Merge the two classes

### Pass-through variables

A variable that travels through many method signatures just to reach one deep consumer.  
**Resolution:** use a context object — store global state in one place, pass the context once at construction.

### Decorators

A decorator extends an existing object with the same API.  
The problem: decorators tend to be shallow (thin wrapper, many pass-through methods).

Before creating a decorator, consider these alternatives in order:
1. **Add to the base class directly** — if the feature is general and used by most callers
2. **Merge with the use case** — if the feature is special-purpose to one context
3. **Merge with an existing decorator** — one deeper decorator beats two shallow ones
4. **Implement as a standalone class** — if the feature doesn't need to wrap

Use decorators only when you cannot modify the wrapped class and need interface adaptation.

### Interface vs. implementation

If the internal representation matches the interface (both line-oriented, both byte-oriented), the class is shallow — it hides nothing.  
The value of a class is precisely the **gap** between interface and implementation.

### Rules

- Each layer must transform or provide something the layer below does not.
- If a class only delegates to another class, merge them or eliminate the delegator.
- If a variable passes through 4+ function signatures to reach its consumer, use a context object.

---

## Together vs. Apart

### Signals that code belongs together

- They **share the same information** (both know a file format, both know a protocol detail)
- They are **always used together** — and the relationship is bidirectional
- They **conceptually overlap** — a natural higher-level category covers both
- You **cannot understand one without reading the other**

### Merge when

- Merging **eliminates information leakage** (shared knowledge in one place)
- Merging **simplifies the interface** (fewer total methods/parameters for callers)
- Merging **removes duplication** of non-trivial logic

### Separate when

- General-purpose code is **mixed with special-purpose code** — separate them
- Two pieces have **no shared information** and no dependency

### Method splitting: depth over length

**Do not split a method because it is long. Split only when the result is a cleaner abstraction.**

Splitting adds complexity: more interfaces, more indirection, more context-switching when reading.  
A 100-line method with a simple interface is better than five 20-line methods you must read together.

**Split when:**
- A sub-task is cleanly separable and the extracted method makes sense entirely on its own
- The extracted method is general enough to be used from other callers
- After splitting, parent and child can each be understood **without reading the other**

**Do not split when:**
- The caller must call both halves sequentially (complexity moves up)
- The halves share so much state they can only be understood together ("conjoined methods")
- The result is shallow methods with more interface than implementation

> **"Depth over length. Don't sacrifice depth for length."**  
> "Every function should be tiny" (Clean Code) creates classitis at the method level.

### Rules

- Do not split a class because it is "large." Split when it contains two distinct pieces of information.
- Do not create a helper method unless it is independently understandable and has multiple (or plausible future) callers.
- Repeated non-trivial logic is always a signal to extract — but into a **deep** function, not a shallow one.
- Before splitting a long method, ask: "Can I understand each piece independently?" If no, keep them together.

---

## Error Handling: Define Errors Out of Existence

Exception handling is one of the worst sources of complexity.  
The goal is to **minimize the number of places where exceptions must be handled.**

### Four strategies, in order of preference

| Strategy | When to use | Example |
|---|---|---|
| **Define away** | Redefine the API so the condition is no longer an error | `unset x` succeeds even if `x` doesn't exist; `substring` returns what overlaps |
| **Exception masking** | Handle internally; caller never sees it | TCP retransmits lost packets transparently; NFS retries until server recovers |
| **Exception aggregation** | One handler catches all related errors at the top | Web server's top dispatcher handles all missing-param errors in one place |
| **Crash** | Unrecoverable; continuing would be worse or meaningless | Out-of-memory; internal invariant violated |

### Rules

- Before throwing an exception, ask: "Can I redefine this operation so the situation is not an error?"
- Do not throw exceptions for conditions the caller cannot reasonably handle differently.
- Do not expose many fine-grained exception types. Aggregate at a meaningful boundary.
- Errors that must propagate should be caught once at the top — not at every intermediate level.
- "Detect everything and throw" is not defensive programming — it is complexity amplification.
- A class with many exceptions is a shallow class. Exceptions are part of the interface cost.

---

## Design It Twice

For every significant design decision, consider at least **two fundamentally different approaches.**

### Why

- First ideas are rarely the best.
- Comparing alternatives forces you to articulate what matters: simplicity, generality, performance.
- Discovering why one alternative is bad teaches you what a good design requires.
- Smart people are most at risk of skipping this — "I'll get it right the first time" is the trap.
- Even smart people who have been successful with first ideas will eventually face problems too hard for that approach.

### How

1. Sketch two or three approaches at the **interface level** — do not fully implement.
2. List the trade-offs of each: simplicity, generality, efficiency, caller burden.
3. Choose the best one, or synthesize a new design from what you learned by comparing.

This applies at all levels: API shape, module decomposition, error strategy, data structure choice.

### Rules

- Before writing the first line of a non-trivial module, draft two interface designs.
- If the first design feels obviously correct, deliberately construct one alternative anyway.
- Document why you chose the design you chose (one sentence in a comment or PR description).

---

## Naming

Names are the primary tool for reducing cognitive load.  
A name is an abstraction: it must convey the essence of the entity, not just be "roughly correct."

### Core rules

- A name must create an **accurate mental image** without requiring the reader to look at declarations or documentation.
- Prefer **specific** over **generic**: `numActiveConnections` not `count`; `nextWriteOffset` not `pos`.
- If you cannot find a good name, the design is probably wrong — the concept itself is unclear.
- **Same concept → same name**, everywhere, always. Never use synonyms for the same thing.
- **Different concepts → different names.** Do not reuse a name for two different things (the `block` bug).
- Remove words that add no information: `fileObject` → `file`, `setValueOfField` → `setField`.

### Consistency — three requirements

1. Always use the common name for a given purpose.
2. Never use the common name for any other purpose.
3. The purpose must be narrow enough that all variables sharing the name have identical behavior.

Violation of rule 3 caused the file system bug: `block` meant both physical-block and file-block. One was used where the other was needed.

### Distance rule

The farther a name's declaration from its use, the longer the name should be.  
Short loop variable names (`i`, `j`) are fine when the entire loop fits on screen.  
If you can't see declaration and use simultaneously, use a longer, more descriptive name.

### Names can also be too specific

An argument named `selection` in a method that deletes a text range implies the argument must be the current UI selection. But the method works on any range. Use `range`.  
Over-specificity in names creates false assumptions about constraints.

### Anti-patterns

| Bad | Better | Why |
|---|---|---|
| `tmp`, `data`, `info` | specific name | conveys nothing |
| `handleStuff` | `retransmitLostPacket` | names the action precisely |
| `flag`, `flag2` | `isConnected`, `hasError` | booleans must be predicates |
| `block` (used for two things) | `fileBlock`, `diskBlock` | same name, different behavior = bugs |

---

## Comments

Comments record **what the code cannot say** — not what it already says.

### What to comment

- **Why**, not what: design decisions, constraints, non-obvious invariants, trade-offs
- **Interface contracts**: what the caller must provide, what the callee guarantees, units, null semantics, error conditions, side effects
- **Surprising behavior**: anything that would cause a competent reader to be wrong on first reading

### What not to comment

- Anything immediately obvious from the code itself
- Implementation details in interface-level comments
- A restatement of the code in prose

### Comment-first design

Write the interface comment **before** writing the implementation.

Procedure for a new class:
1. Write the **class interface comment** first
2. Write the **public method signatures + their interface comments** (no bodies yet)
3. Iterate until the abstraction feels right
4. Write **instance variable declarations + comments**
5. Fill in **method bodies**, adding implementation comments as needed

If a comment is hard to write, the interface is hard to understand — redesign.  
A long or vague comment for a simple method is a signal the abstraction is wrong.  
**Comments are the canary in the coal mine for complexity.**

### Comments belong in code, not commit logs

Commit messages are rarely read. Important information placed only in a commit log will be lost.  
If future developers need to understand *why* a decision was made, put it in a comment next to the code.  
A copy in the commit message is fine, but the **code is authoritative**.

### No duplication

Each design decision must be documented in exactly one place.  
If a fact is relevant in multiple locations, document it once and add a reference elsewhere:  
`// See the comment on Foo.bar for the explanation of this constraint.`

If the master comment moves and the reference becomes stale, the staleness is immediately obvious — the referenced location doesn't exist. Duplicated comments that fall out of sync give no signal.

### Rules

- Do not generate comments that restate the code.
- Generate interface comments that describe behavior from the **caller's perspective**.
- When a design decision is non-obvious, add a one-line "why" comment.
- After modifying code, verify that adjacent comments still accurately describe the new behavior.
- Flag missing interface comments on public APIs as a review issue.

---

## Code Clarity

Code is read far more than it is written. **Optimize for the reader, not the writer.**

### Clarity techniques

- Use **blank lines** to separate logical blocks; precede each block with a brief comment.
- Match declared types to actual types (do not declare `List` when you assign `ArrayList`).
- Do not use generic containers (`Pair<A, B>`) — define named structs with meaningful field names.
- Obey reader expectations: if a constructor spawns threads, say so prominently in a comment.
- Avoid event-driven control flow unless necessary; when used, comment exactly when each handler fires.

### Consistency

Pick one convention and apply it everywhere.  
**Do not "improve" a local convention.** Consistency beats local perfection.  
If an existing convention is wrong, change it everywhere or leave it alone — never make it inconsistent.

### Invariants

An **invariant** is a property of a variable or data structure that is always true.  
Example: "every line in the text buffer ends with a newline character."

Invariants are one of the most powerful consistency tools:

- An invariant lets you reason about all uses of a structure at once — you don't need to check edge cases that the invariant rules out.
- If an invariant holds, special-case code for the corner case it eliminates disappears entirely.
- Enforce invariants in a single module; never let callers violate them.

Invariants are a form of information hiding: the fact that the corner case cannot exist is hidden inside the module.

---

## Modifying Existing Code

Strategic mindset applies to modifications just as much as to new code.

### Stay strategic

When changing existing code, the goal is not the smallest possible change.  
The goal is: **when the change is done, the system should look as if it had been designed with this change in mind from the start.**

Resist the "minimal patch" instinct. Each minimal patch adds a special case, a dependency, or a layer of indirection. Over many patches, the design degrades permanently.

### Comment hygiene

- **Keep comments co-located with the code they describe.** A comment far from its code will not be updated when the code changes.
- Place comments in the implementation file (`.c`, `.cpp`) next to the code — not only in the header file. Developers editing the implementation will see them; developers editing only the header will not.
- Place sub-step comments immediately above each sub-step, not all at the top of the method.
- The more abstract a comment is, the easier it is to maintain — it does not track code details, only overall intent.

### Diff check before committing

Before committing, scan every changed line.  
Verify that nearby comments still accurately reflect the new behavior.  
Also catch: accidentally left-in debug code, unresolved TODOs, typos.

### Rules

- When you touch code, leave it slightly better. Fix small nearby problems encountered during the task.
- If a design problem is found but cannot be fixed now, add a TODO comment with enough context to act on later.
- Comments belong in code. Do not substitute commit-log documentation for code-level documentation.
- Each design decision: documented once, referenced from other places.

---

## Performance

**Simple, deep code tends to run faster than complex, shallow code.**

This is not accidental: complex code usually does redundant or unnecessary work, and shallow layers add overhead without benefit.  
Designing for simplicity and designing for performance pull in the same direction.

### When performance is a concern

- Do not micro-optimize everywhere. Identify the **critical path** — the code executed in the most common case — and optimize there.
- Measure before modifying. Programmer intuition about bottlenecks is unreliable even for experienced developers.
- Minimize special-case checks on the critical path. Ideally, a single check at the top gates all edge cases, and the fast path runs without further branching.
- After optimization, measure again. If the change did not produce a measurable improvement, revert it — do not keep complexity that pays nothing.

### Simplicity as the optimization strategy

Before reaching for algorithmic optimization, ask: "Can I make this code simpler first?"  
Removing shallow layers, eliminating redundant checks, and enforcing invariants frequently produce 1.5–2× speedups with no algorithmic change — and the result is simpler, not more complex.

---

## Decide What Matters

Good design separates what matters from what doesn't, and organizes the system around what matters.

### Find leverage

Seek design choices where **one decision resolves many problems** or **one piece of knowledge explains many behaviors**.

Examples:
- A general `insert/delete` text API handles backspace, delete, paste, search-replace — high leverage.
- A `backspace()` method handles only backspace — low leverage.
- An invariant on a data structure lets you reason about all its uses at once.

**More leverage = more important = more worthy of prominence in the design.**

### Minimize what matters

Fewer things mattering = simpler system.
- Minimize parameters: choose defaults that work for the common case.
- Hide what can be hidden: if a module handles something internally, callers need not know it exists.
- Aggregate what must be exposed: handle exceptions in one place, not many.

### Emphasize what matters

Once you identify what is important, make it visible:
- **Prominence**: put it in the interface, in the name, in a prominent parameter — not buried in options.
- **Centrality**: the most important thing should shape the structure around it, not be a detail.

### Two failure modes

| Failure | Symptom | Result |
|---|---|---|
| **Treating too much as important** | Many caller-visible parameters, shallow classes, over-configuration | Cognitive overload, complexity |
| **Failing to recognize importance** | Hidden critical information, missing essential features | Unknown unknowns, bugs, duplicate workarounds |

### Rules

- Ask: "Does this design decision have high leverage — does understanding it explain many other things?"
- High-leverage decisions belong in prominent positions: interface names, top-level abstractions.
- Low-leverage details belong hidden inside implementations.
- When you find yourself exposing a rarely-needed detail to all callers, pull it down or make it optional.

---

## Domain Boundaries and Language

POSD gives the criteria for reducing complexity. DDD (Domain-Driven Design)
helps decide which concepts belong at the center of a system and where their
boundaries should sit. They are not competing principles. **Use DDD to capture
meaningful boundaries and language, and POSD to verify those boundaries are deep
and simple.**

A domain is not unique to business applications. System software has domains
too. Where enterprise software's domain is business concepts like order,
payment, customer, conversation, and settlement, system software's domain is
system concepts the user must understand precisely: context, handle, socket,
message, buffer, ownership, lifecycle, timeout, error code.

Read the DDD terms in this section with these meanings.

- An entity has an identity and changes state over time.
- A value object means more by its value than its identity. Equal values are
  treated as equal.
- An aggregate is a boundary that owns state and invariants together — state
  that must change as one unit, such as a conversation, room, or workflow
  instance.
- A bounded context is the boundary within which a term keeps the same meaning.
- An application use case is the flow that coordinates several domain objects,
  repositories, channel, and actor calls to handle one external request.
- A port is the interface expressing what an application use case expects from
  external storage, external services, or runtime capabilities.
- An adapter is the edge code that converts between the outside world and the
  domain: HTTP, socket, codec, framework callbacks.
- A policy or process is a rule or flow where one event triggers the next
  command.

### What DDD helps with

The DDD perspective answers these questions.

- Are the names of core concepts consistent across code, docs, and tests?
- Is the boundary that owns state and lifecycle clear?
- Are ownership, state transitions, and failure meanings managed within one
  module?
- Are domain rules kept free of adapter details like transport, codec, storage,
  and framework callbacks?
- Is the public contract expressed in language meaningful to the user, not in
  internal implementation terms?

In enterprise software, concepts like `Order`, `Conversation`, `Invoice`, and
`PlayerQuest` can become domain boundaries. In system software, the boundaries
are message-buffer ownership, handle lifecycle, identifier representation and
comparison rules, and the error meaning of timeout and nonblocking.

### Design procedure

When applying DDD and POSD together, work in this order. The key is to find
domain facts first, then decide code structure, and finally verify the depth of
that structure with POSD.

1. Write the domain flow with event storming first.
   - Record the significant events the user sees, in past tense. E.g.
     `ConversationOpened`, `AgentAssigned`, `MessageSent`, `ConversationClosed`.
   - For system software, record state transitions and contract events instead
     of business events. E.g. `BufferAllocated`, `MessageMoved`, `HandleCreated`,
     `PeerConnected`, `ReceiveTimedOut`, `HandleDestroyed`.
   - Do not fix class, table, or function names yet. First surface "what
     happens."

2. Attach commands and actors.
   - Record the request or intent that produced each event as a command. E.g.
     `OpenConversation`, `AssignAgent`, `SendMessage`.
   - Record the actor that starts each command. E.g. customer, agent, timer,
     remote peer, application caller.
   - If a command can fail, record its failure event or error contract too.
   - If an event triggers the next command, record the policy or process
     between them. E.g. after `ConversationOpened`, if an agent is available,
     run `AssignAgent`.

3. Find entity, value object, and aggregate candidates.
   - Event clusters sharing the same identity and lifecycle are entity
     candidates.
   - Things whose value is the meaning, needing no identity, are value object
     candidates.
   - State clusters that must hold an invariant together are aggregate
     candidates.
   - In system software, view handle, message buffer, identifier, socket
     endpoint, and descriptor — anything that holds lifecycle or ownership in
     the public contract — the same way.

4. Set bounded-context and adapter boundaries.
   - Find the range within which the same word keeps the same meaning.
   - Push other protocols, codecs, storage, framework callbacks, and external
     APIs out to adapter boundaries.
   - If a domain object starts to know external technology directly, the
     boundary is leaking.

5. Define application use cases.
   - Record which aggregates, ports, channels, actors, and timers must be
     coordinated to handle one external request.
   - Policies and processes found in event storming are use-case or
     domain-service candidates.
   - A use case coordinates; it does not implement domain rules directly.
   - An adapter only decodes requests, encodes responses, and wires framework
     callbacks.

6. Re-examine through the POSD lens.
   - If a new layer only forwards requests, remove it or redistribute
     responsibility.
   - If mappers and classes grew for domain purity but caller burden and change
     complexity did not drop, merge them.
   - If an aggregate interface is as complex as its implementation, redraw the
     boundary.
   - If important decisions like lifecycle, ownership, timeout, and error
     contract are scattered across many places, gather them into one module.

This procedure is not only for large enterprise applications. In system
software too, putting events like message ownership transfer, handle
create/destroy, socket connect/disconnect, and receive timeout first makes it
clearer which concepts the public contract should be organized around.

### Choosing an architecture

After finding the domain boundaries, you must lay them out as code structure.
Architecture is the default placement strategy for this. Do not pick an
architecture first and force the domain into it. Find the boundaries with event
storming and domain modeling first, then choose the architecture that protects
those boundaries most simply.

For enterprise software, make hexagonal architecture the default, because
business rules and use cases must outlive HTTP, queues, databases, UI, and
external APIs. Put domain and application use cases at the center, and external
technology as adapters. Define ports by the capabilities the application needs,
not by the shape of the external technology.

```text
dependency direction: outside -> in (adapters depend on application-defined ports)

+--------------------------------+
| Adapters                       |
| HTTP, Queue, DB, External API  |
+--------------------------------+
              | port
              v
+--------------------------------+
| Application Use Cases          |
+--------------------------------+
              |
              v
+--------------------------------+
| Domain Model                   |
| Aggregate, Entity, Value       |
+--------------------------------+
```

Hexagonal architecture also needs a POSD review. If a port and adapter only
forward requests, they are a shallow layer. Remove them, or grow their
responsibility into a deep interface that hides the external technology's
details and is easy for the application to use.

For system software, make layered architecture with a public-contract/runtime
split the default. The core of a system API is keeping the public contract
stable for a long time while leaving runtime, transport, codec, and platform
implementation free to change. The public contract is the surface users learn
and depend on; the runtime is the implementation that absorbs internal
complexity to satisfy that contract.

```text
+--------------------------------+
| Public Contract                |
| API, ABI, Spec, Bindings       |
+--------------------------------+
              |
              v
+--------------------------------+
| Runtime Boundary               |
| Lifecycle, State, Ownership    |
+--------------------------------+
              |
              v
+--------------------------------+
| Integration Layers             |
| Transport, Codec, Platform     |
+--------------------------------+
```

In system software, the public contract holds only the meanings the caller must
know: ownership, lifecycle, timeout, cancellation, error contract. Runtime data
structures, queue implementation, transport wiring, and codec details are
hidden so they do not leak into the contract. A layered structure, too, must
have each layer provide a different abstraction. If the public API and the
runtime boundary repeat the same names and behavior and only forward, one of
them is unnecessary or the responsibility is split wrong.

### Enterprise software application

In enterprise software, event storming is the tool that surfaces the business
flow. Events express the business outcomes the user perceives, in past tense.
For example, write business-meaningful changes first, like `OrderPlaced`,
`PaymentApproved`, `ShipmentRequested`, `ConversationClosed`.

Then attach commands and actors to find the user's intent and the responsible
party. `PlaceOrder` is started by a customer; `ApprovePayment` may be started by
a payment service or operator. Find entities and aggregates in event clusters,
and split bounded contexts where the same word keeps the same meaning. Even if
order, payment, shipping, and settlement all use the word `Status`, do not force
them into one model when the meaning differs.

An application use case coordinates several aggregates, repositories, external
services, and domain events. Domain objects keep business rules and invariants;
adapters handle HTTP, queue, database, UI, and external-API connections. In the
POSD review step, check whether the DDD layers actually own knowledge. If
`OrderService` is a shallow wrapper that only calls `Order.Approve()`, remove it
or give it clear coordination responsibility like payment approval, inventory
reservation, and event publication.

### System software application

In system software, event storming is the tool that surfaces the public
contract's state transitions and ownership rules. Events are not business
outcomes but system state changes the caller observes or is responsible for.
For example, write events like `HandleCreated`, `BufferMoved`, `SocketBound`,
`PeerDisconnected`, `ReadTimedOut`, `ResourceClosed` first.

Commands become API calls or internal runtime requests. For example, record
which events calls like `CreateHandle`, `SendMessage`, `PollReadable`,
`CloseResource` produce and which error contracts they carry. Entity and
aggregate candidates are not business objects but system concepts that hold
lifecycle and ownership: handle, buffer, connection, session, descriptor.

Split bounded contexts along boundaries where meaning changes: runtime,
transport, codec, storage, binding. Be especially careful with words like
timeout, cancellation, backpressure, and ownership, which easily take different
meanings per layer.

Do not carry business DDD vocabulary over verbatim. Avoid names like
`ContextAggregate`, `SocketRepository`, `MessageDomainService` when they do not
help the caller, and instead make these clear:

- Which object owns the lifecycle?
- Who frees memory and handles?
- Which calls are valid after close, destroy, move?
- Is the name for the same concept identical across every public API, binding,
  and doc?
- Do error codes express state transitions and caller responsibility
  consistently?
- Are meanings like timeout, cancellation, backpressure, and reconnect kept
  from being interpreted differently per layer?

A deep system API absorbs the internal complexity of these decisions and
exposes only a simple lifecycle and a consistent error contract to the caller.

### What POSD filters out

Putting DDD names on something does not make it good design. POSD keeps a DDD
structure from turning into excess layers and shallow modules.

Bad application:

```text
Controller -> ApplicationService -> DomainService -> Aggregate
```

If each layer only forwards the same request, the names look like DDD but the
design is shallow. In that case, merge the layers, or redistribute
responsibility so each layer actually owns different knowledge.

Good application:

- An aggregate or lifecycle owner keeps state transitions and invariants
  directly.
- An application use case coordinates several aggregate, actor, channel, and
  storage calls.
- An adapter converts external input into application-use-case or domain-object
  calls, and does not let framework or transport details flow into the domain.
- A public API surfaces domain language and hides internal data structures and
  protocol details.

### Rules

- First write events, commands, actors, and failure meanings with event
  storming. If you cannot name something, the concept is unclear.
- Find entity, value object, and aggregate candidates in event clusters, and set
  boundaries by lifecycle and invariants.
- For enterprise software, make hexagonal architecture the default: put
  domain/application at the center, then push external technology out to
  adapters.
- For system software, make layered architecture with a public-contract/runtime
  split the default, separating the public contract from the internal execution
  structure.
- Keep domain rules inside domain objects or lifecycle owners. Adapters only
  convert and wire.
- After building a DDD design, inspect it with POSD. Remove forwarding-only
  layers or grow their responsibility.
- Do not blindly add mappers and classes just to keep the domain pure. Split
  only when boundary protection actually reduces complexity.
- In system software, treat lifecycle, ownership, state transition, and error
  contract as strictly as a domain model.

---

## Test Coverage Baseline

Tests are part of the design. They make behavior explicit, keep contracts from
drifting, and let future changes remove complexity without guessing.

The default target for test coverage is **80% line coverage**. This is a
baseline, not a substitute for judgment:

- Coverage must prioritize public contracts, protocol compatibility, lifecycle
  boundaries, error paths, timeout/abort behavior, backpressure, and sample
  regressions.
- A high coverage number does not prove quality if important user-visible
  behavior is untested.
- Falling below 80% requires an explicit reason, such as generated code,
  platform-specific glue, or code that is better covered by integration or
  contract tests.
- Do not add shallow tests only to raise the percentage. Tests should explain
  useful behavior and reduce the risk of future changes.

When a module exposes a public API or cross-language contract, prefer focused
contract tests over broad implementation-detail tests. The goal is to protect
the module's guarantees while keeping the implementation easy to refactor.

---

## Red Flags Checklist

Check for these warning signs when generating or reviewing code.  
Each flag signals design debt that will compound.

| # | Red Flag | Diagnostic Question |
|---|---|---|
| 1 | **Shallow module** | Does the interface cost as much to learn as the implementation saves? |
| 2 | **Information leakage** | Is the same design decision reflected in multiple modules? |
| 3 | **Temporal decomposition** | Is the structure based on execution order rather than information ownership? |
| 4 | **Overexposure** | Must callers know about rarely-used features just to use common ones? |
| 5 | **Pass-through method** | Does the method do anything other than forward arguments? |
| 6 | **Repetition** | Does the same non-trivial logic appear in multiple places? |
| 7 | **Special-general mixture** | Is special-purpose logic tangled with general-purpose logic? |
| 8 | **Conjoined methods** | Must you read method A to understand method B? |
| 9 | **Comment repeats code** | Does the comment say exactly what the code already says? |
| 10 | **Implementation in interface comment** | Does the interface comment expose implementation details? |
| 11 | **Vague name** | Does the name fail to create a precise mental image? |
| 12 | **Hard-to-name** | Is it difficult to find a good name? (the concept itself may be unclear) |
| 13 | **Hard-to-describe** | Does the interface comment need to be long? (interface may be too complex) |
| 14 | **Non-obvious code** | Can a competent reader understand the code's behavior at a glance? |
| 15 | **Domain boundary leak** | Are domain rules mixed with adapter, transport, codec, or storage details? |
| 16 | **DDD-named shallow layer** | Does the layer have a plausible name but actually only forward requests? |

---

## Design Principles Summary

1. Complexity is incremental — sweat the small stuff.
2. Working code is not enough. Design for the long term.
3. Invest continuously in small design improvements.
4. **Modules must be deep** — powerful function behind a simple interface.
5. Design interfaces for the most common use case first.
6. **Simple interface > simple implementation.**
7. **General-purpose modules are deeper** — over-specialization is the largest source of complexity.
8. Separate general-purpose from special-purpose code; push specialization to the edges.
9. Different layers must have different abstractions.
10. **Pull complexity downward** — the module developer suffers so callers don't.
11. **Define errors out of existence** where possible.
12. **Design it twice** — always compare at least one alternative.
13. Comments explain what the code cannot — not what it already says.
14. Software must be designed to be **read, not written**.
15. The unit of incremental development is an **abstraction**, not a feature.
16. **Decide what matters.** Emphasize it. Minimize and hide what doesn't.
17. **Make domain boundaries and language explicit.** Use DDD to capture
    meaningful boundaries and POSD to filter out shallow layers.
18. Default test coverage target is **80%**, but contract and risk coverage are
    more important than the raw percentage.

---

> "Good design makes software faster to develop and more enjoyable to work on.  
> The investment pays back — sooner than you think."
