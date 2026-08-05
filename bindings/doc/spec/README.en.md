---
title: "Bindings API Policy"
---

<!-- bindings-nav:start -->
[Spec index](README.md)
<!-- bindings-nav:end -->

# Bindings API Policy

> **What this chapter defines** — the public API policy that applies across
> all of `bindings/`. The per-language documents (`c/`, `cpp/`, `java/`,
> `dotnet/`, `node/`, `python/`, `go/`, `rust/`) align to this policy.

> The implementation baseline for request-reply, SPOT routed, and Actor
> dispatch follows the current public contract in `core/include/zlink.h`.
> Actor dispatch is an independent public service-layer capability, like
> SPOT, and the public surface described in each per-language document
> aligns to this shared contract as well.
> See `c/`, `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/`
> for the per-language interface signatures and usage examples.

| Section | Covers |
|---|---|
| [Purpose](#purpose) | This document's scope and the meaning of the Required/Target notation |
| [Binding Contract Category Policy](#binding-contract-category-policy) | Contract category classification |
| [Binding Runtime Category Policy](#binding-runtime-category-policy) | Runtime category classification |
| [Actor/Spot Route Surface](#actorspot-route-surface) | Route lookup result types, and Actor-targeted send/request |
| [High-Performance Binding Policy](#high-performance-binding-policy) | Hot-path constraints |
| [Substrate vs Public Binding Surface](#substrate-vs-public-binding-surface) | The boundary between the part substrate and the aggregate public surface |
| [`*_part` Substrate Usage Requirement (Required)](#_part-substrate-usage-requirement-required) | Why an aggregate implementation must use the `*_part` API |
| [Spot Get-Or-Create Mapping](#spot-get-or-create-mapping) | The `zlink_spot_node_spot_get_or_new` mapping rule |
| [Public vs Internal API Boundary](#public-vs-internal-api-boundary) | The contract/runtime separation principle and its test |
| [Core Alignment Rules](#core-alignment-rules) | Alignment rules against the core contract |
| [Actor Dispatch Binding Contract](#actor-dispatch-binding-contract) | The public Actor dispatch surface |
| [Document Interpretation Rules](#document-interpretation-rules) | How to read the Required/Target notation |
| [Core Principles](#core-principles) | The core principles that run through this whole policy |
| [Monitor Ready Contract](#monitor-ready-contract) | The meaning of monitor readiness |
| [POSD Structure Policy](#posd-structure-policy) | The deep-module, low-change-amplification structural standard |
| [Public Surface Rules](#public-surface-rules) | Operation naming, builder, and terminator rules |
| [Domain Object Policy](#domain-object-policy) | The value-type-vs-interface test |
| [Socket Type Capability Policy](#socket-type-capability-policy) | The capabilities each socket family exposes |
| [Per-Language Spec File Compliance Rule](#per-language-spec-file-compliance-rule) | The relationship between the per-language documents and this document |
| [Service Layer Policy](#service-layer-policy) | The public contract for the SPOT/Actor service layer |
| [Core API Additions](#core-api-additions) | Binding coverage for recently added core capabilities |
| [Option Policy](#option-policy) | Rules for exposing socket/context options |
| [Performance Policy](#performance-policy) | Shared performance standards across all bindings |
| [Boundary Cost Policy](#boundary-cost-policy) | The FFI/marshalling boundary-cost standard |
| [Peer Weight Policy](#peer-weight-policy) | The peer-weight contract |
| [Monitor Policy](#monitor-policy) | The monitor event/snapshot contract |
| [Error Policy](#error-policy) | Error representation and domain mapping |
| [Length And Range Boundary Policy](#length-and-range-boundary-policy) | Value validation and boundary limits |
| [Ownership Policy](#ownership-policy) | Message/handle ownership rules |
| [Naming Policy](#naming-policy) | Shared naming rules across languages |
| [Compatibility Policy](#compatibility-policy) | The ban on compatibility shims and deprecated wrappers |
| [Cross-Language Alignment](#cross-language-alignment) | How to verify consistency across languages |
| [Test Policy](#test-policy) | Test scope and standards |
| [Test Matrix](#test-matrix) | A language-by-capability coverage table |
| [Sample Policy](#sample-policy) | Sample code standards |
| [Perf Policy](#perf-policy) | Perf-runner standards |
| [Script Location Policy](#script-location-policy) | Where test/sample/perf scripts live |
| [Review Checklist](#review-checklist) | Checks to make during PR review |
| [POSD-Based Implementation Completeness Policy](#posd-based-implementation-completeness-policy) | The test for declaring an implementation complete |
| [Implementation Review Checklist](#implementation-review-checklist) | Checks before declaring an implementation done |
| [Binding Requirements](#binding-requirements) | Requirements every binding must satisfy |
| [API Reference](#api-reference) | Standards for generating API reference documentation |
| [Disconnecting A Peer By Routing ID](#disconnecting-a-peer-by-routing-id) | The routing-id-based peer disconnect contract |
| [Related Documents](#related-documents) | Links to related documents |
| [Core API Surface 6.0.0 Alignment](#core-api-surface-600-alignment) | Alignment status for the 6.0.0 core API surface |
| [Spot Route Bridge API](#spot-route-bridge-api) | The route bridge API contract |

## Purpose

This document defines the public API policy for all of `bindings/`.

Its purpose is to prevent each language binding from growing its own
surface and its own exception rules, and instead to enforce a shared
contract that can always be explained in terms of
`core/include/zlink.h`.

This document does not claim that every binding is already in this state
today. It sets the `.NET` binding's contract/runtime separation and file
granularity as the standard target, and gives the remaining wrapper
bindings a baseline to align to in stages. Items marked `Required` apply
immediately in the current review; structures and surfaces marked
`Target` apply as the goal of the work that aligns that binding. The C
binding is the native ABI baseline, so it follows a separate set of
exceptions.

The documents under `c/`, `cpp/`, `java/`, `dotnet/`, `node/`, `python/`,
`go/`, `rust/` define the public API contract each binding implementation
must actually provide externally. What these documents govern is the
public types, methods, signatures, return values, and error semantics —
the public interface a binding implementation exposes must not diverge
from this contract. Every wrapper binding except C also separates the
public contract from the runtime implementation. This separation is fixed
by responsibility, but the physical directory and package/module path
follow each language's conventions. The actual path each per-language
README specifies is the implementation baseline for that binding.

This document is not a simple style guide. It is a design-standard
document for:
- public API design standards
- review standards
- refactoring standards
- sample and test standards

Its intent is to:
- eliminate APIs that look similarly named across languages but carry
  different meaning
- eliminate shallow surfaces that expose the same capability through
  multiple redundant paths
- reduce raw option bags, unnecessary convenience wrappers, implicit
  ownership, and hidden error paths
- let a binding user avoid needing to know internal sequencing, native
  detail, or hidden transport switches
- drive deep modules and low change amplification, per POSD principles
- tie correctness together with the cost model, sample quality, and
  testability under one shared standard

The baseline is always `core/include/zlink.h`. Each binding follows the
core contract, and may choose its representation to fit language
convention — but the semantic contract must not change.

This document defines "what each language must guarantee," not "how each
language may look."

## Binding Contract Category Policy

Every binding must split its public contract into the same semantic
categories. The actual folder, package, namespace, or module name can be
adjusted to fit language convention, but which category a given public
type belongs to must be judged by the same standard across bindings.

The point of this policy is not to make file placement look tidy. It is
to let a user find a concept they learned in one language, at the same
location and with the same meaning, in another language. So the contract
subcategories follow the conceptual boundaries of the public API, not the
implementation file structure.

| Category | Purpose | Includes |
|------|------|-----------|
| `core` | The library-wide foundational contract | Public types not tied to a specific socket or service, such as `Context`, `ContextOptions`, `RoutingId`, and version/capability lookup |
| `messaging` | The message data and receive-result contract | Payload types independent of socket kind, such as `Message`, `Received`, topic message, subscription event, and multipart payload helpers |
| `sockets` | The socket-kind and socket-operation contract | `PairSocket`, `DealerSocket`, `RouterSocket`, `PubSocket`, `SubSocket`, `StreamSocket`, socket interfaces, send/recv/publish/request/reply builders, socket options |
| `eventing` | The waiting, event-source, and observation contract | `Poller`, `PollEvent`, timer, monitor socket, monitor event, monitor snapshot |
| `service` | The core service-layer contract | Public types that belong to a service domain, such as Spot and Actor dispatch |
| `errors` | The public error and failure-representation contract | Base exception, bind/connect/send/recv/submit/config/request exceptions, public error-code/result mapping |

### Contract Category Rules

- The `contract`/`runtime` split is the split between the public API and
  implementation detail. A contract subcategory must not mirror the
  runtime's internal structure as-is.
- Keep `core` small. A type that can only be explained by knowing a
  specific domain belongs in that domain's category, not in `core`.
- `service` can have subdomains such as `spot` and `actor`. Create a
  subdomain only when a user must learn it as an independent concept.
- `eventing` does not mean monitoring alone. It also holds public
  contracts for waiting on or observing events, such as poller, timer,
  and monitor.
- `errors` holds error surfaces shared across multiple domains. A type
  whose meaning is strongly domain-specific, such as the result of a
  particular socket operation, belongs in that domain instead.
- Do not make a representation-format category such as `enums` a
  canonical category. Enums, flags, and results belong in the category of
  the public concept that interprets their value.
- Put operation, result, and callback helper types in the domain that
  defines their meaning. For example, send/request/reply results and
  callbacks belong in the messaging contract, while Actor
  join/session/management results and callbacks belong in the service
  contract. A snapshot entry stays with the service model that returns
  that snapshot.

### Handler Registration Naming Policy

A callback/handler registration function's name must reveal what it
actually does. A name that reads like a function invoked when an event
occurs, used for a registration function, can make a user unsure whether
it's a hook they must implement or an API that stores a handler.

- A public API that stores a single handler for one subject, or replaces
  the existing handler, uses a `set...Handler`-family name. Spell it per
  language convention: `Set...Handler`, `set...Handler`, or
  `set_..._handler`.
- A public binding's `set...Handler` keeps only one active handler per
  subject. Calling the same setter again replaces the current handler. A
  raw native attach conflict or a recv-mode conflict may still be
  reported as a separate error, but the public setter name does not imply
  cumulative registration.
- Only a public API that accumulates multiple handlers uses an
  `add...Handler` or `register...Handler`-family name.
- An `on...`-family name is reserved for a protected/internal hook or a
  framework-level handler method invoked when an event occurs. It is not
  the canonical name for a handler-registration function.
- An API that changes protocol state, such as a topic subscription, may
  use `subscribe`/`unsubscribe`. A function that simply stores a callback
  does not use `subscribe...Handler`.
- Do not create a surface that unregisters by setting a callback to
  `null`/`None`. When unregistration is needed, handle it through the
  close/lifecycle rules instead.

The representative canonical semantic names are:

| Meaning | Canonical name |
|------|----------------|
| Registering a send-ready handler | `setSendReadyHandler` |
| Registering a raw STREAM packet handler | `setPacketHandler` |
| Registering a SPOT dispatch event handler | `setDispatchHandler` |
| SPOT routed receive | `recvRouted` |
| SPOT Actor lifecycle receive | `recvActorLifecycle` |

The representative enum/result/flags placement rules are:

| Example type | Category | Reason |
|---------|------|------|
| `SendFlags`, `RecvFlags`, `SubmitResult`, `RecvResult` | `sockets` | Describes the input to or result of a socket operation. |
| `PollEventFlag`, `PollSourceKind`, `MonitorEventType` | `eventing` | Describes an event-wait or observation result. |
| `SpotDispatchEvent`, `SpotPeerKind` | `service.spot` | Its meaning is defined only inside the Spot service domain. |
| `ConfigResult`, `ErrorCode` | `errors` | Describes a failure meaning shared across multiple domains. |

Every wrapper binding shares the same architecture map. This map is not a
baseline for copying one language's folder names literally — it lets a
concept learned in one language be found at the same responsibility
location in another language's code. The actual file names, directory
names, and package/module/import paths follow per-language convention and
public API stability.

The shared architecture map is:

```text
contracts/
  core/
  messaging/
  sockets/
  eventing/
  service/
  errors/

runtime/
  native/
  sockets/
  messaging/
  eventing/
  service/
  errors/
  buffers/
  options/
  handles/
```

In this map, `contracts` is the public contract surface a user reads
first. `contracts` holds the types a user directly depends on: public
interfaces, public value objects, public result/flag/error types, and
public builders/facades. Implementation detail stays hidden in `runtime`.

Do not overuse public interfaces, however. Value objects and plain data
types such as `Message`, `RoutingId`, `Received`, `TopicMessage`, and
enum/result/flags are not split into separate interfaces. Interfaces
belong only where a user needs to receive something polymorphically —
for example, a common socket role, a poll target, a monitor target, a
codec, a handler/callback, or a SPOT client role.

`runtime` is the implementation area that executes the public contract.
It hides implementation decisions such as the socket send/recv flow,
message materialization, the poller/timer/monitor loop, the service
runtime, native interop, and buffer/handle/error mapping. A runtime type
is not recommended as public API, and a user should not depend on it
directly without going through the contract surface.

### Interface / Implementation Separation Policy

Every wrapper binding except C follows the `.NET` binding's direction of
separating the public interface/contract from the runtime implementation.
Separating "like `.NET`" here does not mean every language copies names
such as `IContext` or `Contracts`/`Runtime` literally. It means the
contract a user sees and the implementation detail — native calls, handle
owners, callback bridges, request pumps — live in separate areas of
responsibility.

The separation rules are:

- Types, interfaces, traits, protocols, abstract roles, factories,
  builder start points, DTOs, value objects, enums, and error/result
  types that a user depends on go in the public contract source.
- A type that directly owns a native handle, calls the core helper
  substrate, manages a callback trampoline and request progress, or
  performs marshalling goes in the runtime or native bridge source.
- For a resource type where it's more natural for a user to depend on a
  role than on an implementation — `Context`, socket, poller, timer,
  SpotNode, Spot, Actor — separate the contract role from the default
  implementation in whatever way the language supports.
- Value-centric types such as `Message`, `RoutingId`, `Received`,
  `TopicMessage`, snapshot DTOs, and enum/flags/result are not wrapped in
  a meaningless interface/trait/protocol. Keep a value type as a concrete
  public type, and hide implementation detail inside the type itself when
  it needs internal native-backed storage.
- Even in a language where a runtime concrete class must be publicly
  exposed, the behavior contract a user needs to understand must be
  explained in the public contract source first.
- Write samples, perf, and framework adapters against the public contract
  projection, not against a runtime concrete type or native bridge.

This separation is not just a naming split. A file on the `contracts`
side must be readable without knowing the native handle, native function
names, request pump, callback trampoline, or buffer-marshalling sequence.
Conversely, a file on the `runtime` side implements the public contract,
but must not itself become a public surface a user has to import.

The same standard applies to file structure.

- Use a category aggregate file only as a small re-export barrel or for
  factory wiring. If a single category file such as `sockets`, `service`,
  or `eventing` holds the actual behavior of several public resources,
  the contract/runtime split has not happened.
- Put a native-backed resource's implementation in its own per-resource
  file. For example, each socket family, poller, timer, SpotNode, Spot,
  and Actor should have its own implementation file. The file name
  follows language convention but must reveal the resource or operation
  name.
- A shared helper file is not a substitute location for a public
  resource's implementation. A helper file should hold only lower-level
  functionality shared across multiple implementations, such as a native
  call wrapper, handle validation, a marshalling helper, or error
  mapping. Do not collect a resource's actual behavior — for `Context`,
  `RouterSocket`, `SpotNode`, `Poller` — into a helper file.
- A contract file and a runtime file do not need a strict 1:1 mapping,
  but for any given public resource, its contract owner and runtime owner
  must each be clear.

### File Granularity Policy

Every wrapper binding also matches a similar granularity when splitting
files. The goal is not to replicate file count or file names 1:1, but to
let a reader of any language find the same conceptual grouping in a
similarly sized file.

The baseline is the `.NET` binding's `Contracts` organization level. One
file holds either one independent public concept, or a small, tightly
coupled group of contracts that share the same reason to change.

The file-splitting rules are:

- A resource contract a user looks up directly — `Context`, a socket
  family, `SpotNode`, `Spot`, `Actor`, poller, timer — can have its own
  file even if it's thin.
- A type where ownership, storage, value validation, or cost model
  matters — `Message`, `Received`, `TopicMessage`, `RoutingId` — gets its
  own file.
- A type with a weak independent reason to change — a marker interface,
  delegate, small enum, one-line record — merges into the nearest
  contract file. For example, a socket marker role merges with the
  socket base contract, and a stream packet handler delegate merges with
  the stream socket contract.
- Staged operation builder contracts such as send/request/reply share the
  same domain-level reason to change, so they can be grouped into one
  operation contract file.
- A group with a clear service subdomain — Actor join, actor management,
  SpotNode snapshot models — gets its own domain file. Split it further
  only once the model file grows large enough that distinct reasons to
  change appear, such as peer/status/socket/actor snapshots.
- The same principle applies to runtime implementation files. One
  implementation file holding several native-backed resources' lifecycle,
  send/recv/request flow, callback registration, and snapshot mapping all
  at once is too broad. Split such a file into per-resource
  implementation files and a shared helper file.
- Keep a category barrel small. As a rough guideline, once a barrel holds
  hundreds of lines of resource implementation beyond re-exports and
  simple factory wiring, treat that as a split failure. The real
  criterion is the reason to change, not the line count — if editing
  different public resources always means opening the same file, split
  it.
- Do not create a file based only on a representation format or a
  catch-all name such as `Enums`, `Types`, `Models`, `Common`, or `Utils`.
  A file name must reveal the domain concept a user is looking for, or
  the reason it changes.
- Each language follows its own casing, suffix, and package convention.
  For example, C# might spell it `OperationContracts.cs`, Rust
  `operation_contracts.rs`, and TypeScript `operation-contracts.ts` — but
  the same grouping of responsibility must be preserved.

When applying this standard, the alignment approach declared by the
per-language README takes priority. If a per-language README declares a
breaking alignment, aligning to the canonical surface takes priority over
preserving the existing public surface. Prefer to cleanly rearrange the
namespace, package export, crate re-export, package `exports`, or
generated declaration surface when moving files. Do not create a new
public wrapper or a shallow compatibility shim just to match the file
structure.

The per-language application follows these principles:

- `.NET` treats `Contracts/<Category>` and `Runtime/<Category>` as its
  standard structure. Public interfaces and public value objects go in
  `Contracts`; implementation classes and native interop helpers go in
  `Runtime`.
- Because a Java package is close to public API, Java puts public
  interfaces/value objects under `systems.zlink.contracts.<category>`,
  and implementation classes and native bridges under
  `systems.zlink.runtime.<category>` or
  `systems.zlink.runtime.nativeapi`. It does not create a `FooContract`
  interface just to list methods.
- C is the native ABI baseline, so it does not create separate
  contract/runtime folders. It expresses the same categories through
  header files, header sections, and documentation sections.
- C++, Go, Rust, Python, and Node follow each language's module/package/
  export convention, but must still distinguish the public-facing surface
  from the runtime implementation. When a language does not naturally
  support interfaces, the same distinction must be made explicit in the
  documentation and the export surface.

If an existing binding has a `monitoring` or `Monitoring` category, treat
`eventing` as the canonical category. The name `monitoring` was
sufficient while only the monitor API existed, but `eventing` is the
broader and more accurate concept for a public contract that also covers
poller and timer. When cleaning up structure, describe new documents and
new files as `eventing` responsibility. An already-public `monitoring`
import/export path may be kept as a temporary alias only when the
matching per-language README explicitly commits to preserving
compatibility. A binding that has declared a breaking alignment cleans up
to `eventing` and does not keep a `monitoring` alias.

A representation-format folder such as `enums` is not a top-level
category in the shared architecture map. Enums, flags, results, and
literal unions belong in the domain category that interprets their
value. For example, `RecvFlags` belongs to the `sockets` contract,
`PollEventFlags` to `eventing`, and `SpotPeerKind` to the `service`
contract.

## Binding Runtime Category Policy

A wrapper binding separates the public contract from the runtime
implementation. Runtime is the implementation layer that performs the
actual behavior behind the contract surface. The public contract shows a
user what they can call, and runtime handles that call against the
native substrate and the language's own execution model.

A runtime subcategory does not need to be strictly 1:1 with a contract
subcategory. But the implementation responsibility and reason to change
must be clear, and it must not grow into a thin pass-through class that
merely repeats the public contract.

The recommended runtime categories are:

| Category | Responsibility |
|------|------|
| `native`, or a per-language equivalent name | P/Invoke, JNI, FFI, native function declarations, ABI type conversion, native symbol loading |
| `handles` | Native handle ownership, dispose/close, lifetime, reference tracking |
| `messaging` | Native message part assembly, multipart handling, message conversion, request progress |
| `sockets` | Socket operation execution, send/recv/publish/request/reply flow |
| `eventing` | Poller, timer, monitor, event dispatch loop |
| `service` | Spot, Actor service runtime |
| `options` | Public option validation, native option mapping |
| `errors` | Converting native errno/result into public exception/result |
| `buffers` | Byte buffer, direct buffer, pooled buffer, pinned memory, copy/borrow policy |

The name may differ because of a language reserved word. For example,
because `native` is a keyword in Java, it can use `runtime/nativeapi`
instead. Names can differ, but the responsibility must still be
explainable.

### Runtime Category Rules

- Runtime does not promise public API stability. The public contract is
  defined by the contract documentation and the per-language public
  surface.
- A runtime implementation class hides behind a public contract interface
  or a public facade. If a user has to construct or call a runtime class
  directly, the contract design is leaking.
- If a contract interface only repeats the runtime implementation's
  method list 1:1, that's a shallow-module warning sign. Create an
  interface only where there's a real role abstraction, and never for a
  value object.
- Split runtime categories by reason to change. For example, adding a
  native symbol should change `native`; a send/recv flow change should
  change `sockets`; a message-ownership change should change `messaging`
  or `buffers`.
- Do not use a catch-all name such as `core`, `common`, `utils`,
  `internal`, or `misc` as a canonical runtime category. These names make
  it easy to mix unrelated reasons to change into one place.

If an existing runtime has a `monitoring` or `Monitoring` category, the
canonical category is `eventing`, the same as for contract. Even a file
that only holds a monitor implementation belongs under `eventing` if it
shares a reason to change with the poller, timer, or event dispatch loop.

From a POSD perspective, this standard aims to get both public-surface
readability and implementation information hiding at once. The contract
gives users a small, clear surface to learn, while runtime absorbs
implementation decisions such as how native calls are made, handle
ownership, buffer pooling, and error mapping. Do not create an
abstraction, however, when it doesn't actually reduce a real role and
only repeats a method list — that adds complexity instead of reducing it.

## Actor/Spot Route Surface

Every binding must expose the core's Actor route and Spot route results
without loss. Per-language type names can differ, but the following
meaning must be preserved.

- An Actor route exposes the Actor ref's node rid, current Spot rid, and
  current Spot kind.
- A Spot route exposes the looked-up Spot rid, owner node rid, and Spot
  kind.
- Spot kind distinguishes Entry Spot, user Spot, and an invalid value.
- A binding does not create a new direct `router -> actor` or
  `actor -> router` API. A user combines a route lookup result with the
  existing Spot routed API.

## High-Performance Binding Policy

zlink is a high-performance messaging library. A binding may add
per-language convenience, but it must not hide or worsen the hot path's
cost model. The public API and internal implementation must follow the
principles below.

- Do not use reflection-based dynamic dispatch on the message send/recv,
  publish/subscribe, request/reply, dispatch callback, poller, or timer
  path. Even when a language runtime requires reflection, restrict it to
  initialization or binding-registration time, and never use it in the
  message-processing loop.
- Reflection is not a workaround for a missing API. A high-performance
  binding must use a typed facade, a direct native downcall, or a direct
  internal bridge, and must not add a reflective lookup to the hot path
  just to satisfy the public contract.
- Do not create unnecessary allocation. A repeated call must not
  construct a new temporary array, wrapper, closure, or boxed object of
  the same size every time.
- Do not create unnecessary copies. Move a message part received from
  core into the language's own `Message`-owned object as directly as
  possible, and do not copy the byte buffer again without a decode step
  or an explicit user request.
- Do not put a global lock, coarse lock, avoidable mutex contention, or a
  shared-executor serialization point on the hot path. Limit necessary
  synchronization to the minimum scope that protects per-subject state.
- Do not perform a hidden blocking wait, sleep, busy wait, or thread join
  on the callback, dispatch, poller, timer, or request-completion
  progress path. Only a call explicitly documented as a blocking API may
  wait.
- A binding uses core's `*_part` substrate to build language objects part
  by part. Double materialization — building a native aggregate array and
  then converting it again into a language-specific collection — is
  forbidden.
- Perf, sample, and test code used for performance verification must also
  use only the public binding entrypoint, and must not break the cost
  model above.

This section is not an implementation-detail optimization
recommendation — it's a public binding conformance requirement. If review
finds a reflection hot path, unnecessary allocation/copy, thread
contention, or a hidden wait, that binding is considered non-compliant.

## Substrate vs Public Binding Surface

A bindings implementation sits on top of the helper substrate C API core
provides (the `*_part` family). The public API exposed to a bindings user
does not have to follow that helper's signature shape. What is fixed by
the rule below is which core functions the internal implementation is
allowed to call.

This document interprets the following boundary:

- The `*_part` helper substrate contract in `core/include/zlink.h` is the
  native substrate a bindings implementation must use.
- A document under `doc/spec/bindings/` defines only the
  **public convenience contract** each language binding provides
  externally.

In other words, the binding's public API can look different from the
helper substrate. But how it calls core internally must not differ.

For example, the following structure is required.

- The core substrate has a primitive surface such as `*_part`,
  `has_more`, and a caller-provided `zlink_msg_t`.
- Java, `.NET`, `Go`, `Rust`, `Python`, `Node`, `C++`, and C bindings
  layer a language-friendly public API on top — `Received`, `Message`,
  collections, and request/reply convenience.
- Any path inside the public API that calls core directly must use the
  `*_part` substrate. It must not call an aggregate-shaped core function
  (`zlink_send`, `zlink_recv`, `zlink_publish`, and so on) from inside a
  binding.

The following conditions must always hold:

- A binding's public API semantic contract must be explainable in terms
  of the core contract.
- A binding must not directly expose a low-level detail that exists only
  in the helper substrate.
- A binding must not expose part-by-part receive as a public binding
  API, such as `RecvPart`, `RecvRoutedPart`, `SubscribePart`,
  `recv_part`, `recv_routed_part`, or `subscribe_part`. The binding
  runtime absorbs the part loop, `has_more`, and per-part envelope
  metadata into an aggregate result storage internally.
- A document under `doc/spec/bindings/` does not document the helper
  substrate signature itself as a public contract.
- The helper substrate is treated only as a foundation layer for
  bindings implementation and performance optimization.

In other words, the bindings policy documents are governed not by "what
the helper looks like" but by "what public contract a binding user
ultimately sees."

## `*_part` Substrate Usage Requirement (Required)

The internal implementation of the send, request, reply, publish, and
subscribe function families must use core's `*_part` helper substrate.
This is a `Required` rule.

### Scope

This applies to every binding-internal implementation path in the
following families.

- send (including single-part, multi-part, and routed)
- recv (including single-part, multi-part, and routed)
- request (including dealer, router, and SPOT variants)
- reply (including router and SPOT variants)
- publish
- subscribe (including SPOT subscribe)

### Reason

Back when core provided both aggregate functions and the `*_part`
substrate, calling the aggregate function directly was allowed. But that
structure creates the following cost:

- Core first builds a native aggregate (a parts array).
- The binding then converts that aggregate again into a language-specific
  object (`Message[]`, `Received`, a value object).
- The result is a back-to-back "build the native aggregate → build the
  language-object aggregate" sequence, and this double-conversion cost
  becomes a real bottleneck on the hot path.

Using the `*_part` substrate directly lets a binding convert each part
straight into a language object one at a time, eliminating the native
aggregate-construction step entirely. This produces a measurable
performance difference especially in languages like Java and .NET, where
object materialization is expensive.

This rule is not for the sake of structural tidiness — it is a
requirement meant to **substantially reduce runtime performance cost**.

### The Public API Shape Stays The Same

This rule is about the internal implementation foundation. The public API
shape a binding user sees stays whatever each language's spec defines,
regardless of this rule.

- A user still uses a language-friendly API such as
  `send(List<Message>)`, `recv()`, or `request(...)`.
- The `*_part` call sequence is a binding-internal implementation detail
  and is not exposed to the user.
- A public binding's receive surface offers only an aggregate
  result-storage API such as `recv`, `subscribe`, or `recvRouted`. The
  `RecvPart`/`SubscribePart` family is a name for the performance
  optimization substrate, not a public contract name.

## Spot Get-Or-Create Mapping

Core provides `zlink_spot_node_spot_get_or_new(...)` for the atomic
"get a local logical Spot by routing id, or create it if absent"
contract.

Every higher-level binding must map its public get-or-create SpotNode API
directly onto that C function. It must not compose `spot_lookup()` and
`create_spot()` to emulate the same behavior, because doing so loses
core's atomicity contract and reintroduces the lookup/create race.

The per-language names are:

- C++: `spot_node_t::get_or_create_spot(...)`
- .NET binding: `SpotNode.GetOrCreateSpot(...)`
- Java: `SpotNode.getOrCreateSpot(...)`
- Node: `SpotNode.getOrCreateSpot(...)`
- Go: `SpotNode.GetOrCreateSpot(...)`
- Rust: `SpotNode::get_or_create_spot(...)`
- Python: `SpotNode.get_or_create_spot(...)`

Each wrapper returns both the owned `Spot` facade and whether this call
created the logical spot. The returned facade follows that language's
normal Spot lifetime rules.

### Compliance Check

Confirm the following during implementation review and verification.

- No path in the binding source directly calls an aggregate symbol
  (`zlink_send`, `zlink_recv`, `zlink_send_rid`, `zlink_publish`,
  `zlink_subscribe`, `zlink_router_recv`, `zlink_dealer_request`,
  `zlink_router_request`, `zlink_router_reply`, `zlink_spot_send_*`,
  `zlink_spot_request_*`, `zlink_spot_reply_*`, `zlink_spot_subscribe`,
  and so on).
- The matching `*_part` symbol is used instead
  (`zlink_send_part`, `zlink_recv_part`, `zlink_send_part_rid`,
  `zlink_publish_part`, `zlink_subscribe_part`, `zlink_router_recv_part`,
  `zlink_dealer_request_part`, `zlink_router_request_part`,
  `zlink_router_reply_part`, `zlink_spot_*_part`, and so on).
- Non-compliance blocks the review.

## Public vs Internal API Boundary

Every binding must separate the public contract from the internal
implementation surface. This document and each per-language README
define the public API's boundary and the library's shape. The exact
function, method, and type list is owned by the public contract source
that each wrapper binding's per-language README designates, except for
C. For C++ and .NET, that location is a literal `Contracts/` folder; for
Java, Node, Python, Go, and Rust, it's the package, module, or export
surface each README designates. The installed header, package
entrypoint, `.d.ts`, `__init__.py`, and `lib.rs` re-export are the
projections that expose this contract to the user. C is the exception —
`core/include/zlink.h` is the single baseline for the public C ABI.

The following principles apply to every binding in common.

- Any type, function, method, module, package, or namespace not included
  in the per-language public contract source is treated as internal
  implementation detail.
- A per-language README does not repeat every public member. Instead it
  defines the public contract source location, source layout, API-change
  procedure, runtime/internal boundary, and performance policy.
- An internal API is not enough to merely look internal by name. Where a
  language supports it, use a language-native boundary — package export,
  module export, assembly visibility, crate re-export, package `exports`,
  an `internal/` directory — to actually restrict access.
- In principle, perf, sample, and test code must also use only the public
  binding entrypoint. Being in the same repository does not license a
  direct import of or reference to an internal helper.
- Public contract verification is judged against the entrypoint a
  deployed binding consumer actually sees. The mere existence of an
  internal symbol inside the source tree does not make it public.
- A binding that ships an installed header alongside a compiled binding
  library, like C++, keeps a public `Contracts/` inside the installed
  `include/` tree, and hides the implementation as private files under
  `bindings/cpp/src/Runtime/`. An aggregate header may still exist, but it
  must not become the only entry point for finding a public class.
- The freedom to refactor internal structure is guaranteed, but only
  within the scope that preserves the public contract.

In other words, this document's purpose is not only to define the public
API boundary and library shape — it also includes enforcing that boundary
so a non-public API cannot be used as if it were public.

### Per-Language Contract/Runtime Separation

Every wrapper binding except C must separate the public contract from the
runtime implementation. However, how it separates them must follow that
language's package, module, and import-path rules. A per-language README
must specify both the actual repository path and the actual
package/module path. `Contracts` and `Runtime` are shared logical
category names — they do not mean every language must turn that literal
word into a public package or import path.
C++ is a C++20 binding; its public contract root is
`bindings/cpp/include/zlink/Contracts/` and its runtime implementation
root is `bindings/cpp/src/Runtime/`.
Because a Java package path is itself the source folder, Java reveals the
role structure through lower-case Java packages such as
`systems.zlink.contracts.*` and `systems.zlink.runtime.*`.
Languages such as Go, Rust, and Python, where the folder path connects
directly to the package/module/import path, also separate the public
contract from the runtime implementation inside the actual
package/module tree.
For Node/TypeScript, `package.json` exports set the public boundary, but
because the source folder name can also cause deep-import confusion, it
follows the actual source path and package-export rules its per-language
README specifies.

C is the native C ABI baseline. C's public contract is
`core/include/zlink.h`, and `bindings/c` aligns its sample, test, perf,
packaging, and any necessary mapping policy against that C API. C is not
forced to have a separate `Contracts/`/`Runtime/` layering.

`Contracts` is the role for the public contract source a user must check.
`Runtime` is the role for implementation detail such as a native handle,
callback bridge, request progress pump, helper substrate call, or object
lifetime correction. `Native` is the role reserved for the native
bridge — FFI, P/Invoke, JNI/Panama, N-API, cgo. In a language the
document names explicitly, such as C++ and .NET, these role names are
used as the actual folder names; in other languages, the same role is
expressed through the package/module/export structure the per-language
README specifies.

`Contracts` and `Runtime` are shared role names. That does not mean they
are the public package, namespace, module, or import-path name. A
language where directory structure directly affects the package/module
path does not expose `Contracts` or `Runtime` as a public import path.
Instead, it places the contract at an actual path inside the public
package/module tree, and keeps the runtime implementation inside a
language-specific private boundary such as `internal`, a private module,
an unexported module, or a `pub(crate)` module.

#### Contract / Runtime Placement Rules

The following criteria apply to every wrapper binding except C. Check
this table first when adding a new public API or moving an
implementation.

| Item | Location |
|---|---|
| A public behavior contract a user calls or references by type | The matching category in public contract source |
| The contract for a public constructor, factory, or builder start point | The matching category in public contract source |
| A public free function, static facade, extension helper, or module function | The matching category in public contract source |
| A public builder convenience method or helper | The matching category in public contract source |
| A DTO, value object, enum, or public error/result type | The matching category in public contract source |
| A runtime concrete class, socket kernel, or handle owner | The matching category in runtime/internal source |
| A request progress pump, callback trampoline, or part-loop helper | The matching category in runtime/internal source |
| A native handle wrapper, FFI declaration, struct mirror, or marshalling helper | Native bridge source |
| Generated native loading code, platform artifact lookup | Native bridge source |

The judgment rules are:

- A public contract type's public signature does not reference a native
  bridge type.
- If runtime/internal source needs a user-facing method, add the contract
  to public contract source first. The runtime implementation implements
  or projects that contract.
- If a helper a user calls directly is public — whether it's shaped as a
  class method, static method, free function, extension method, or
  module function — put the contract in public contract source. Do not
  leave it in a runtime-only location just because it's a simple
  convenience function.
- A public factory may return a runtime concrete type. But its
  construction behavior and the user-observable behavior of the returned
  type must be explainable in public contract source.
- Do not expose the runtime/internal folder name or the module/package
  path itself as public API. However, a basic implementation class or
  type such as `Context`, socket, `SpotNode`, `Poller`, or `Timer` may be
  exposed as a per-language public projection. In that case, the public
  behavior a user observes must still be explainable in public contract
  source.
- A public contract type's public signature does not reference a native
  bridge type. Even when a concrete value object's internals must use
  native-backed storage, keep the P/Invoke/JNI/N-API/cgo declaration and
  the marshalling-only struct mirror in native bridge source.
- Keep a value-only DTO/value/enum/error/result type concrete. Do not
  wrap it in a meaningless interface, trait, or protocol for the sake of
  symmetry.

The fixed categories are:

- `Core/`: context, version, roles, and utility resources.
- `Messaging/`: message, routing id, received, topic message, multipart.
- `Sockets/`: socket contracts, socket implementations, socket options.
- `Eventing/`: monitor, poller, timer, readiness events.
- `Service/`: SPOT, actor, SPOT topology.
- `Errors/`: public error/result/exception domains and runtime mapping.
- `Native/`: the native bridge category, kept only under runtime/internal
  source.

These category names are fixed in the documentation and review standard.
The actual file and folder names follow the convention each per-language
README specifies. If a new category is needed, change this shared policy
together with the per-language README structure (except C) before using
it. Do not create a `Native` category in public contract source — the
native bridge always lives under runtime/internal source.

The wrapper binding's shared structure is fixed to the following role
structure. Except for C, each per-language README must show this
structure again using that language's actual repository path and
package/module/import path, and the implementation must match that
structure.

```text
bindings/<lang>/
+-- <public-package-or-module-root>/
|   +-- <public contract categories>
|   |   +-- Core
|   |   +-- Messaging
|   |   +-- Sockets
|   |   +-- Eventing
|   |   +-- Service
|   |   +-- Errors
|   +-- <private runtime/internal area>
|   |   +-- Core
|   |   +-- Messaging
|   |   +-- Sockets
|   |   +-- Eventing
|   |   +-- Service
|   |   +-- Errors
|   |   +-- Native
+-- codecs/
+-- tests/
+-- samples/
+-- perf/
+-- native/
+-- runtimes/
```

The shared standard is:

- The public API contract a user must check should be gathered in an
  easy-to-find location.
- Implementation detail such as a native handle, callback bridge, request
  progress pump, helper substrate call, or object lifetime correction
  must not mix with the public contract.
- Keep a DTO, value object, enum, or error/result object as a concrete
  type. Do not wrap a value-only type in a meaningless interface or
  trait.
- A type that hides a native resource and its behavior — socket, context,
  monitor, timer, service node, spot, actor — may have an abstraction
  boundary that fits the language's convention.
- In principle, write perf, sample, and framework adapters against the
  public contract too. Depending on a runtime-internal type just because
  it's in the same repository weakens the public/internal boundary.

The per-language application direction is:

| Binding | Application standard |
|---|---|
| C | `core/include/zlink.h` is the single baseline for the public C ABI. `bindings/c` does not add a separate contract/runtime layer, and aligns only the C-API-based mapping, sample, test, perf, and packaging policy. |
| C++ | `bindings/cpp/include/zlink/Contracts/` is the public C++ contract location. `bindings/cpp/src/Runtime/` is the private implementation location. It prefers C++20, RAII classes, and concrete values, and does not over-wrap a public class in a virtual interface. |
| .NET | Detailed standards follow the [.NET binding blueprint](dotnet/README.md). This document does not duplicate .NET's detailed file structure. |
| Java | The public contract package under `bindings/java/src/main/java/systems/zlink/contracts/` is the public contract location. Because Java follows URL-based package layout, it reflects the lower-case `contracts` and `runtime` packages in the actual folders. The native bridge lives under the non-exported `systems.zlink.runtime.nativeapi`. |
| Node | `bindings/node/src/index.ts` and the `package.json` exports are the public contract projection. Contract source lives at a lower-case source path such as `bindings/node/src/zlink/contracts/`, and the runtime/native addon implementation is hidden under `bindings/node/src/zlink/runtime/`. |
| Python | `bindings/python/src/zlink/contracts/` is the public contract source. The `zlink` root package is the projection that re-exports this contract, and the native/FFI implementation lives under private packages such as `_runtime/` and `_native/`. |
| Go | The `bindings/go/contracts/` public package is the Go public contract source. Currently the runtime/native implementation is owned by unexported implementation files at the root and by cgo bridge files. If it is split into a separate package later, it should be hidden under Go's `internal/` convention. |
| Rust | `bindings/rust/src/contracts/` serves as the public contract source. `lib.rs` re-exports the necessary types as a crate-root/domain projection, and `bindings/rust/src/runtime/` and `bindings/rust/src/runtime/native/` stay private modules. |

A review is not judged by simply "does an interface exist," but by the
following questions.

- Can a user understand the usable API just by looking at the public
  contract?
- Does the public contract avoid directly requiring a runtime concrete
  type, native handle, or helper bridge type?
- Does the file hold an independent concept, or a group that shares the
  same reason to change?
- Could a thin file that holds only a marker, delegate, small enum, or
  one-line record be merged into a nearby contract file instead?
- Has abstracting a value type blurred equality, ownership, or the cost
  model instead of helping?
- Does it use the language ecosystem's natural encapsulation mechanism?

#### Per-Binding Target Physical Layout

Each per-language README treats the path and role below as the target for
new alignment work. If the current implementation still differs from
this structure, align it in stages together with that binding's API,
sample, and perf work during its structural cleanup. This does not mean
the public package, namespace, module, or import path directly exposes
the `Contracts` or `Runtime` name below.

| Binding | Contract root | Runtime root | Public projection |
|---|---|---|---|
| C++ | `bindings/cpp/include/zlink/Contracts/` | `bindings/cpp/src/Runtime/` | `#include <zlink.hpp>` and installed `include/zlink/...` headers |
| .NET | See [dotnet/README.md](dotnet/README.md) | See [dotnet/README.md](dotnet/README.md) | See [dotnet/README.md](dotnet/README.md) |
| Java | `bindings/java/src/main/java/systems/zlink/contracts/` | `bindings/java/src/main/java/systems/zlink/runtime/` | exported `systems.zlink.contracts.*` JPMS packages and Maven artifact |
| Node | `bindings/node/src/index.ts` and `bindings/node/src/zlink/contracts/` | `bindings/node/src/zlink/runtime/` | package root export, generated `.d.ts`, and `package.json` exports |
| Python | `bindings/python/src/zlink/contracts/` | `bindings/python/src/zlink/_runtime/` and `bindings/python/src/zlink/_native/` | `zlink` package exports from `__init__.py` |
| Go | `bindings/go/contracts/` public package | current root unexported implementation files and cgo bridge files; future split should use `bindings/go/internal/...` | exported identifiers in `zlink.systems/zlink/contracts` |
| Rust | `bindings/rust/src/contracts/` | private `bindings/rust/src/runtime/` and `bindings/rust/src/runtime/native/` modules | `lib.rs` re-exports and public rustdoc projection |

Each per-language README must show where the `Core`, `Messaging`,
`Sockets`, `Eventing`, `Service`, and `Errors` roles are actually placed
in its source. `Native` exists only as a runtime/native bridge role and
is never made a public contract role.

### Package / Namespace Identity Policy

The official library domain is `zlink.systems`. Any per-language package,
namespace, module, or artifact name being newly fixed or changed must
start from this domain, and must not put a prior organization name or a
repository owner's name into a canonical public identifier.

| Binding | Canonical public identity |
|---|---|
| C | The public header is `zlink.h`; the symbol prefix is `zlink_` |
| C++ | The namespace is `zlink`; the installed header root is `include/zlink/` |
| .NET | The NuGet package id and root namespace are `Systems.Zlink` |
| Java | The Maven group id, JPMS module, and root package are `systems.zlink` |
| Node | The npm package is `@zlink-systems/zlink`; the public entrypoint is the package root |
| Python | The distribution name and import package are `zlink` |
| Go | The module path is `zlink.systems/zlink`; the public package is `zlink` |
| Rust | The crate name and public crate root are `zlink` |

- A framework extension package and namespace stays under that
  framework language's canonical identity. For example, `.NET` uses
  `Zlink.Framework.*`, and Java uses `systems.zlink.framework.*`.
- Go, Python, and Rust are not currently framework targets, so they do
  not add a binding-owned codec module.
- A Node extension package's name follows ecosystem convention, but its
  public identity must not drift outside the `zlink` and
  `zlink.systems` domain.
- New documents, samples, and tests use only the canonical identity.
- Even if an old `Zlink` root namespace or package id remains for
  implementation compatibility, it is not the canonical public identity,
  and no new public API is added under it.

### Core Interface Shape Rules

This section summarizes the required public interface shape for every
wrapper binding except C. See the recv section and operation builder
section further below for the detailed contract. C keeps the functional
ABI of `core/include/zlink.h` as-is, so this wrapper rule does not apply
to it.

- The data-plane `recv` and `subscribe` families take caller-provided
  output storage. The caller creates a result object such as `Received`,
  `TopicMessage`, or `SubscriptionEvent`, and the binding updates that
  object.
- A data-plane receive's return value expresses only "was data
  received." A hard error is delivered as a typed exception, `error`, or
  `Result`, per language convention.
- A control-plane API such as `Monitor.recv` or `Timer.recv` is called
  infrequently and returns a small result, so a per-language nullable,
  optional, or value-return form is allowed.
- A service control/admission receive such as `Spot.recvActorJoin` is
  also not a data-plane drain path, so a per-language nullable, optional,
  or result-value form is allowed. However, no-data and a hard error must
  still be separated, and the public contract must clearly document this
  exception.
- `send`, routed send, `publish`, `request`, `reply`, SPOT
  send/request/reply, and the Actor location/session-attach family return
  an operation builder.
- A builder start point's arguments take only the operation's target —
  destination, topic, channel, routing id, or request sequence. Payload,
  flags, timeout, callback, and the async/callback submit choice are
  expressed at the builder stage.
- Multipart payload accumulates through repeated `message(...)` calls on
  the builder. A `messages(...)` convenience may exist per language
  convention, but the canonical path is the builder. If such a
  convenience is public, it is part of the builder contract and belongs
  in `Contracts/`.
- Do not multiply operation-start names such as `sendNoWait`,
  `publishWithFlags`, `requestAsync`, or `requestCallback`. Keep the same
  operation name, and let the builder stage absorb the variation. The
  per-language final execution method for an async or callback completion
  surface follows the
  [bindings async execution surface policy](async-coroutine-policy.md).
- Resource creation is not scattered across public constructors on
  several runtime classes. A per-binding root facade or context factory
  owns construction responsibility. For example, the .NET binding
  creates a context with `Zlink.CreateContext()`, and creates socket and
  service resources through `IContext.Create...` factories.
- A runtime concrete type must not appear directly in a public contract
  signature. A public method's arguments and return value must be
  explainable through a contract interface, value object, DTO, enum, or
  result/error type.
- Samples, perf, and framework adapters use only this canonical
  interface. Do not write new code against a runtime-internal helper or a
  legacy overload.

### The Send/Recv Public Shape Is Fixed

The public `send`/`recv` shape of the bindings is not something to
redecide every time the substrate helper's shape changes. It is fixed to
the public shape this document and each per-language binding spec
define.

In other words, even if the helper substrate's shape changes — `*_part`,
`has_more`, caller-provided message storage — the binding's public API
must keep the following principles.

- A binding user sees the `send`, `recv`, request/reply, and callback
  shape defined in the language document.
- Multipart can continue to be offered through whatever aggregate
  convenience model each language document defines.
- A binding's public `send`/`recv` shape must not be shaken up just
  because the helper substrate changed.
- Changing the public shape must be treated as a public API change
  separate from introducing the helper, and the `doc/spec/bindings/`
  document must be updated first.

In other words, even if a helper C API is introduced going forward, a
binding's `send`/`recv` is "the implementation foundation changing," not
"the shape the user sees changing automatically."

### Canonical Recv: Caller-Provided Storage

For a high-level binding (C++ / .NET / Java / Node / Python / Go / Rust),
the data-plane recv surface's canonical form is a **ref-out shape that
takes a caller-pre-built result storage as a parameter and updates its
internal state**. A shape that allocates and returns a new result
instance on every call forces hot-path allocation overhead, so it is not
used as the canonical surface.

This rule is `Required`. When building a new binding or updating an
existing one, the canonical recv surface must satisfy this section.

#### Scope (all data-plane recv)

| Surface | Result type (caller storage) |
|---|---|
| `MessageSocketBase.recv` (PAIR / DEALER) | `Received` |
| `RoutedMessageSocketBase.recv` (ROUTER) | `Received` |
| `StreamSocket.recv` | `Received` |
| `SubscriberSocketBase.subscribe` (SUB / XSUB) | `TopicMessage` |
| `XPubSocketBase.receiveSubscriptionEvent` | `SubscriptionEvent` |
| `Spot.subscribe` | `TopicMessage` |
| `Spot.recv` (routed) | `Received` |

`Monitor.recv` (`MonitorEvent`) and `Timer.recv` (`uint64`) are
control-plane calls, called infrequently and with a lightweight value
result, so they are not in scope for this section. They keep a
return-form (or a per-language `Optional`/nullable/`Option`). A service
control-plane API that receives an Actor join admission request, such as
`Spot.recvActorJoin`, can apply the same exception. In that case, the
public contract must document the no-data representation and the hard
error representation separately.

#### Base Contract

- The `recv` caller pre-builds a long-lived result storage and passes the
  same instance on every call. The binding reuses its internal part
  collection, routing-id storage, and topic buffer as much as possible,
  driving per-recv allocation toward zero.
- The return value carries only "was something received" — a boolean, or
  an equivalent representation that distinguishes success from no-data.
  A hard error is delivered as an exception or error code, per language
  convention.
- When a call with a non-blocking flag such as `recv_flags_t::dontwait`
  finds no data, it returns a no-data representation used together with
  caller-provided storage, such as `false`, `recv_result_t::no_data`,
  `(false, nil)`, or `Ok(false)`. It does not signal EAGAIN as an
  exception.
- A multipart result accumulates into the caller's result storage. The
  binding must not build a temporary collection and cache it separately
  from the caller's result storage — that allocation would not go away.
- For routed recv (router / spot), the routing id must be filled into
  storage inside the caller-provided `Received`. A path that allocates a
  new byte array per routing id does not belong on the internal hot
  path.

#### Canonical Per-Language Signature

Apply the same ref-out pattern to each surface in the table above. Below
are examples keyed on the `Received` result type; `TopicMessage` and
`SubscriptionEvent` follow the same pattern.

| Binding | Canonical signature |
|---|---|
| C++ | `int recv(received_t& out, recv_flags_t flags = recv_flags_t::none);` 0 = success; failure or no data returns a `recv_result_t` integer value. If a local failure such as message initialization happens inside the binding, it returns -1 and sets errno. Multipart results fill `out.parts`. `subscribe(topic_message_t& out, int flags)` and `receive_subscription_event(subscription_event_t& out, int flags)` follow the same rule. |
| .NET | `bool Recv(Received result, RecvFlags flags = RecvFlags.None);` `bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);` `bool ReceiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags = RecvFlags.None);` A `Received` storage is created with `Received.Create()`. true = received, false = no data (DontWait). A hard error is `ZlinkException`. |
| Java | `boolean recv(Received result, RecvFlags flags);` `boolean subscribe(TopicMessage result, RecvFlags flags);` `boolean receiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags);` |
| Node | `recv(received: Received, flags?: RecvFlag): boolean;` `subscribe(topic: TopicMessage, flags?: RecvFlag): boolean;` `receiveSubscriptionEvent(event: SubscriptionEvent, flags?: RecvFlag): boolean;` |
| Python | `def recv_into(self, received: Received, *, flags: int = 0) -> bool: ...` `def subscribe_into(self, topic: TopicMessage, *, flags: int = 0) -> bool: ...` `def receive_subscription_event_into(self, event: SubscriptionEvent, *, flags: int = 0) -> bool: ...` |
| Go | `func (s *Socket) Recv(out *Received, flags RecvFlags) (bool, error)` `func (s *Socket) Subscribe(out *TopicMessage, flags RecvFlags) (bool, error)` `func (s *Socket) ReceiveSubscriptionEvent(out *SubscriptionEvent, flags RecvFlags) (bool, error)` |
| Rust | `pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError>;` `pub fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError>;` `pub fn receive_subscription_event(&self, out: &mut SubscriptionEvent, flags: RecvFlags) -> Result<bool, RecvError>;` |

A C ABI binding is not in scope for this section. The C binding exposes
`zlink.h`'s typed substrate (`zlink_router_recv_part`,
`zlink_subscribe_part`, and so on) as-is.

#### Unifying `Received` Envelope Meaning

For a high-level binding (C++ / .NET / Java / Node / Python / Go / Rust),
`Received` is a **shared envelope that holds the result of one
data-plane recv call**. The meaning of request, reply, routed source, and
payload lifecycle must stay the same regardless of socket kind or
service kind.

The rules below are `Required`.

- The receive results of PAIR / DEALER / ROUTER / STREAM / SPOT routed
  recv all use the same `Received` meaning.
- A request-reply receive result must not fork into a separate
  protocol-specific result type. A surface that splits request meaning by
  socket kind into separate public types — `DealerReceived`,
  `RouterReceived`, `SpotReceived` — is not canonical.
- Request meaning is independent of socket kind. If `request_seq` is
  present, the receive result has request-reply context; if not, it's an
  ordinary receive result.
- The reply target, send-back target, and source routing metadata are
  encapsulated inside `Received`'s own context. A user must not need to
  know a socket-kind-specific frame format or internal dispatch rule to
  handle a request.
- Per-language names and optional representations (`null`, `None`,
  `Optional`, `Option`, a zero value plus a `has` flag, and so on) can
  differ, but the canonical field/method meaning must match the
  [Domain Object Canonical Shape](#domain-object-canonical-shape-shared-by-every-binding)
  section.

The C ABI binding is an exception. C does not build a managed/object
result storage; it exposes the same envelope components through typed
out-params such as `zlink_router_recv_part()`, `zlink_spot_recv_part()`,
and `zlink_dealer_recv_part()`. Do not add a public aggregate object such
as `zlink_received_t` to C — doing so would grow message-part ownership,
init/close/reset, and reply-context retention into a new public lifetime
contract. If a C helper is needed, keep it only as a sample/perf/internal
helper.

A per-language detail document may separately list a deprecated overload
kept for backward compatibility. The table above lists only the
canonical path new code and samples/perf must follow.

#### Result-Storage Reuse Contract

- A result storage (Received / TopicMessage / SubscriptionEvent)
  automatically resets its internal state before receiving a new recv
  result. Passing the same instance to `recv` repeatedly is normal usage.
- .NET's `Received` does not expose a public constructor. A caller uses
  `Received.Create()` to build the storage to be filled. `Received` is a
  concrete receive buffer and is not split into a separate read
  interface.
- If a caller calls the next recv without separately `move`-ing the
  previous recv's part messages, the previous message must be closed
  appropriately. The binding provides a separate helper (such as
  `takeFirstPart`) that hands ownership of a part `Message` to the
  caller.
- Thread safety does not guarantee that multiple threads can pass the
  same result storage into recv concurrently. The existing policy that a
  socket is recv'd by a single thread still holds.

### Operation Builder Policy

zlink's send/request/reply/publish family, and the Actor
location/session-attach family, all have many combination axes. Spreading
target path, payload part count, `flags`, `timeout`, and the
async/callback completion mode across plain method overloads makes a
socket or service handle a shallow, wide interface, and forces multipart
payload to be wrapped in an external List/Vector container. A high-level
binding hides this combinatorial complexity inside an operation object,
and multipart naturally accumulates through repeated `message(...)` calls
on the builder.

This policy does not apply to the C ABI binding. The C binding keeps the
functional contract that matches `zlink.h`. It applies to the canonical
public API of high-level bindings such as C++ / Java / .NET / Node /
Python / Go / Rust.

#### Start Points In Scope

An operation builder start point is exposed with the same pattern across
**every send, request, reply, publish, Actor location, and Actor
session-attach surface**. The name is converted to fit language
convention.

##### Spot facade (`Spot` / `spot_t`)

- `publish(topic)`
- `sendToChannel(channelName)` / `send_to_channel(channel_name)`
- `sendToSpot(destNodeRid, destSpotRid)` / `send_to_spot(...)`
- `requestToChannel(channelName)` / `request_to_channel(...)`
- `requestToSpot(destNodeRid, destSpotRid)` / `request_to_spot(...)`
- `requestToRouter(peerRid)` / `request_to_router(...)`
- `replyToSpot(destNodeRid, destSpotRid, requestSeq)` / `reply_to_spot(...)`
- `replyToRouter(peerRid, requestSeq)` / `reply_to_router(...)`
- `replyActorJoin(request, accepted)` (Actor join admission reply)

##### Raw socket facade

- `PubSocket.publish(topic)` / `XPubSocket.publish(topic)`
- `DealerSocket.send()` / `DealerSocket.request()`
- `RouterSocket.send(rid)` / `RouterSocket.request(rid)` / `RouterSocket.reply(rid, requestSeq)`
- `RouterSocket.sendToSpot(destNodeRid, destSpotRid)` / `requestToSpot(...)` /
  `replyToSpot(destNodeRid, destSpotRid, requestSeq)`
- `PairSocket.send()` (PAIR send)
- `StreamSocket.sendTo(rid)` (STREAM peer send)
- Any other raw send-capable socket's send entrypoint exposes an
  operation builder start point the same way.

##### SpotNode/StreamSocket Actor surface

- `SpotNode.joinActor(actor, destNodeRid, destSpotRid)` / `join_actor(...)`
- `SpotNode.leaveActor(actor, currentSpotRid)` / `leave_actor(...)`
- `SpotNode.destroyActor(actor)` / `destroy_actor(...)`
- `SpotNode.remoteActorGetRef(targetNodeRid, actorId)` / `remote_actor_get_ref(...)`
- `StreamSocket.bindActor(sessionRid, actor)` / `bind_actor(...)`
- `StreamSocket.unbindActor(sessionRid, actorId)` / `unbind_actor(...)`
- `StreamSocket.sendBoundActor(sessionRid, actorId)` / `send_bound_actor(...)`
- `SpotNode.sendBoundSessionMsg(actor)` / `send_bound_session_msg(...)`

#### Common Builder Rules

- A start point does not send immediately — it returns a per-language
  operation builder such as `SendOp`, `RequestOp`, `ReplyOp`,
  `ActorJoinOp`, `ActorLeaveOp`, `ActorDestroyOp`, `ActorLookupOp`,
  `ActorBindOp`, or `ActorUnbindOp`. Regardless of which start point is
  used, multipart payload is always expressed through repeated
  `.message(...)` calls.
- A builder convenience such as `.messages(...)`, `.flags(...)`,
  `.timeout(...)`, a callback submit, or the final execution method of an
  async completion is part of the builder contract if it is public. It
  must not be defined only as a runtime-internal shortcut. The
  per-language name and meaning of the async-completion final execution
  method belongs in the
  [bindings async execution surface policy](async-coroutine-policy.md).
- Payload accumulates through repeated `message(part)` calls on the
  builder. A single payload and a multipart payload are not split into
  separate start-point overloads. Multipart is not wrapped in an
  external List/Vector container.
- Do not create a single-payload shortcut overload with the same name as
  a start point. For example, public overloads such as `send(message)`,
  `send(routingId, message)`, `publish(topic, message)`,
  `sendToChannel(channelName, message)`, and
  `sendToSpot(nodeRid, spotRid, message)` are forbidden. Express all of
  these through builder steps, such as
  `send(...).message(message).submit()`.
- When a language can naturally express an explicit move/consume name, it
  can add an ownership-transfer step (`moveMessage`, `MoveMessage`,
  `move_message`, and so on) inside the same builder. This step is not a
  new operation start point, and the name and documentation must clearly
  state that the caller cannot reuse that message even after a submit
  failure. This does not change the existing `message(...)` step's
  contract of preserving the original on failure.
- For an operation where payload is semantically required — send,
  request, reply, publish, Actor join, ActorReplyJoin, and so on — a
  `submit` with zero messages is forbidden. A language whose type system
  can prevent this blocks it at compile time; other languages block it
  with a validation error at `submit` time.
- For an operation with no payload — Actor `leave`, `destroy`,
  `bindActor`, `unbindActor`, `remoteActorGetRef` — the builder can submit
  immediately without a `message(...)` step. But it still exposes the
  same builder shape and option steps (`flags(...)`, `timeout(...)`,
  `callback(...)`, the async-completion final execution method).
- `flags`, `timeout`, and the callback/async choice are optional builder
  steps, not start-point parameters. A start point takes only
  semantically key arguments, such as the target address or request
  sequence.
- A messaging call in a sample or documentation example does not repeat a
  default value. For a message-sending function such as `request`,
  `requestToChannel`, `send`, `sendToChannel`, `reply`, or `publish`, use
  the packet name inferred by default from the request object or the
  registered packet type. Use a packet-name override such as
  `.packetName(...)`, `.packet_name(...)`, or `.PacketName(...)` only when
  the packet actually being sent differs from the request type's default
  packet name. The same way, use a per-call timeout such as
  `.timeout(...)` or `.Timeout(...)` only when that call needs a value
  different from the default timeout configured on the socket or
  framework. This rule is not about making an example look shorter — it's
  a sample contract that keeps a user from mistaking an unnecessary
  option for standard usage.
- A helper that directly builds a request/reply protocol envelope does
  not belong on the public binding surface. An API such as
  `requestFrame(...)` exposes the request sequence and frame layout to
  the caller, so it must stay a runtime/internal helper.
- A reply must start from the received request context. The public
  binding API offers only a surface that reveals a reply-capable
  context, such as `received.reply()` or
  `router.reply(peerRid, requestSeq)`. An API such as
  `dealer.reply(requestToken, parts)`, where DEALER starts a reply with
  an arbitrary token, does not belong on the public binding surface. A
  DEALER cannot designate a specific peer routing id, so reply-routing
  decisions would leak into a protocol helper, and the user would need to
  understand token semantics.
- An async request or async Actor operation does not take submit flags. A
  callback form may take `flags` to express a non-blocking submit. The
  detailed difference in completion mode follows the
  [bindings async execution surface policy](async-coroutine-policy.md).
- A builder cannot be submitted again once it has been submitted. A
  language that offers a move-only or ownership type blocks this by
  type; otherwise it is blocked by a runtime state check.
- Because an Actor join start point's admission completion shape differs,
  its builder exposes a dedicated completion result (`ActorJoinResult`)
  that captures both the reply payload and the final Actor ref together.
  lookup/destroy/leave/bind/unbind use the ordinary reply completion
  shape (`RequestResult`).

#### Common Flow Example

Names are converted to fit language convention.

```java
spot.publish(topic)
    .message(part1)
    .message(part2)
    .flags(SendFlags.DONTWAIT)
    .submit();

routerSocket.requestToSpot(destNodeRid, destSpotRid)
    .message(reqPart)
    .timeout(Duration.ofSeconds(3))
    .submit();

spotNode.joinActor(actor, destNodeRid, destUserSpotRid)
    .message(joinStatePart)
    .timeout(Duration.ofSeconds(3))
    .submit(joinCallback);

streamSocket.bindActor(sessionRid, actorRef)
    .timeout(Duration.ofSeconds(2))
    .submit(replyCallback);
```

#### Per-Language Async Execution Surface Standard

The per-language final execution method for an async or callback
completion belongs in the
[bindings async execution surface policy](async-coroutine-policy.md).

This rule is Required under the POSD standard. When adding or cleaning up
a new send/request/reply/publish or Actor location/attach public API,
use this operation builder shape and the async execution surface policy
as the baseline, and do not grow the existing overloads into more
canonical APIs.

## Core Alignment Rules

This section is a summary of the core contract that takes priority over
the detail examples in per-language documents. If there is a mismatch
between `core/include/zlink.h` and a per-language document, this section
is the baseline.

#### Direct Receive Callback Constraints

- The direct receive callback install surface exists only for raw
  `STREAM` and SPOT routed receive.
- A binding must not publicly expose an `onReceive`-style direct data
  callback for raw `PAIR`, `DEALER`, or `ROUTER`.
- A binding must not publicly expose an `onSubscribe`-style direct topic
  callback for raw `SUB`, `XSUB`, or SPOT subscribe receive.
- `ROUTER` inbound routed traffic is received through a single routed
  recv surface. The binding runtime uses `zlink_router_recv_part()`
  internally, and exposes only the aggregate routed recv and the request
  completion callback on the public surface. It does not provide a
  direct receive callback.
- Core's raw `STREAM` is an exceptional type that picks one of three
  modes: `recv`, the raw callback (`zlink_recv_handler()`), or the packet
  callback (`zlink_stream_packet_handler()`). A high-level binding's
  canonical public contract exposes only the `recv` and packet-callback
  surfaces. The raw direct callback is used only as an internal binding
  primitive — adding it as public API requires changing this policy
  document and the matching language spec together first, splitting it
  out as a separate raw/low-level surface.

#### SPOT Channel And Dispatch Surface

- SPOT is a channel-aware model. A binding must provide
  `create_route_bridge(...)` or an equivalent typed bridge,
  `create_publisher(...)` or an equivalent publisher handle,
  `send_to_channel`, `send_to_spot`, `request_to_channel`, the
  channel-aware send/request operation builder start points, and the SPOT
  topic publish/subscribe surface. Legacy surfaces that attach an
  external channel `DEALER`, route mesh `ROUTER`, or raw `PUB` socket
  directly to a `SpotNode` are not part of the public contract.
- A SPOT subscribe result exposes topic/parts. The channel name is not
  repeated as a message result field.
- `zlink_spot_dispatch_event_handler()` is the canonical readable
  notification surface for the SPOT topic/routed/channel-reply/timer/actor
  planes.
- The Actor dispatch surface is a public service-layer capability, same
  as SPOT. Every binding exposes it through public types that fit its
  own language convention, and the shared meaning follows the
  `Actor Dispatch Binding Contract` section and the `Actor Dispatch
  Policy` section below.

#### Auto-HWM And SpotNode Options

- `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` and
  `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES` must be exposed by every
  binding as a typed context option. The profile value is one of
  compact, low latency, balanced, or throughput, and the default is
  balanced. A context message-unit default of `0` means using each
  socket type's default message unit.
- `MonitorStatus` must expose every one of core `zlink_monitor_status_t`'s
  auto-HWM v2 diagnostic fields without omission. Enabled, the profile
  enum, role, policy class, unit budget, size cap, socket message slots,
  effective message bytes, applied HWM, the recent recalculation reason
  enum, deferred shrink, and blocked ratio are all part of the public
  snapshot contract.
- A SPOT node option name follows core's public enum as-is. A binding
  does not expose a directional HWM option or a delivery-queue
  hard-limit option. What it exposes is the four admission options
  `ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE`,
  `ZLINK_SPOT_NODE_OPT_ROUTER_HWM`,
  `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE`,
  `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM`, and the two dispatch-worker options
  `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN` and
  `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX`.
  The C API's shared `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` stays only as an
  explicit override on a raw socket. A per-language high-level binding
  does not expose this value on a socket/SpotNode/Spot public facade — it
  exposes only the context option as the canonical API. A SPOT node or
  SPOT handle cannot set the raw socket's shared option; calling it fails
  with `EINVAL`. This value is not a message-size limit — it's the
  planning unit used to turn an auto-HWM budget into a slot count.
  A dispatch-worker option adjusts only the size of the callback worker
  pool owned by `SpotNode`, and does not mean `ZLINK_IO_THREADS` or a
  data-plane thread count. `min` must be at least 1, and `max` must be at
  least `min`. Absent explicit configuration, it maps to `min=max=1` when
  there is 1 CPU, and otherwise to `min=2`, `max=cpu_count`.
#### SPOT Status And Snapshot Names

- A SPOT binding status object must expose core's
  `disconnected_sub_target_count` and `disconnected_routed_target_count`
  under a name that fits language convention. Because core currently does
  not disconnect a target from delivery-queue growth alone, both values
  currently report `0`.
- When a SPOT binding exposes or documents an internal socket snapshot
  name, it uses the public snapshot name core returns as-is. The current
  names are `mesh-pub`, `mesh-xsub`, `peer_ctrl_pub`, `peer_ctrl_sub`,
  `routed-router`, `local-pub`, and `internal_receiver`. `local-pub` is
  the local fanout socket that sends to a subscriber inside the same
  node. (`ingress-sub`, `pub-ingress-tx`, `internal-router`, and
  `internal-router-tx` have been removed and are not part of the
  snapshot.)
#### Dispatch Readiness Meaning

- `zlink_spot_dispatch_event_handler()` is the single entry point for
  SPOT routed receive and Actor lifecycle readiness. A binding does not
  expose a direct routed callback as public API.
- `ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE` and
  `ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE` are readiness
  notifications, not message-count notifications. A binding must not
  describe or implement them as edge-triggered one-shot events.
- `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE` and
  `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE` belong to the same
  dispatch-readiness axis. An Actor readable event must let the caller
  know which Actor to drain, and an Actor join readable event must be
  drained through `Spot`'s join receive surface.
- A SPOT dispatch consumer must reflect, in its documentation and
  samples, the rule of draining `subscribe`/`recv_routed` until each
  language's no-data representation appears. For example, C++ uses the
  `recv_result_t::no_data` return value, and Java/Node/Python use `false`
  from the API that fills caller-provided result storage.
- The first SPOT routed recv must not perform hidden activation, hidden
  queue open, or hidden target registration. A binding assumes the same
  premise and does not layer lazy bootstrap logic on top.
#### Send-Ready, Peer Weight, And STREAM Receive Modes

- `zlink_send_ready_handler()` and the poller's `ZLINK_POLLOUT` point to
  the same send-recovery readiness axis. A binding's documentation must
  describe them with the same meaning. `ZLINK_POLLOUT` is described as
  "send recovery readiness / backpressure recovery notification," not as
  "transport writable."
- A binding must expose the peer-weight surface as a per-language typed
  option/property. It applies to `ROUTER` and `DEALER`; the value range
  is `0..10000`, and the default is `100`. `0` means exclusion from new
  outbound selection. The matching submit failure code is
  `ZLINK_SUBMIT_NOT_ADMITTED` (value 13), and it must be included in
  every binding's `SubmitError` mapping.
- Core's raw `STREAM` can select only one of three receive modes at a
  time: (a) blocking/non-blocking recv based on `zlink_recv_part()`,
  (b) the raw direct callback `zlink_recv_handler()`, or (c) the packet
  callback `zlink_stream_packet_handler()`, which uses big-endian
  `u16 header_size + u32 body_size + header + body` framing. A second
  attach returns `EBUSY`. A high-level binding must keep the same
  mutual-exclusion rule among the `STREAM` receive surfaces it exposes
  publicly. A separate public release API such as `detachStream`,
  `streamDetach`, or a callback detach is not in core's public contract,
  so it is not added to the canonical binding surface. Releasing the
  receive mode and cleaning up the callback is handled by socket close.
- `zlink_recv_handler()` is exclusive to raw `STREAM`. Attaching it to
  `PAIR`/`DEALER`/`SUB`/`XSUB`/`ROUTER` fails with
  `ZLINK_HANDLER_NOT_SUPPORTED`.
- Socket defaults: `ZLINK_ROUTER_OPT_MANDATORY` = `1`,
  `ZLINK_OPT_RID_DUPLICATE_POLICY` = `ZLINK_RID_DUPLICATE_REJECT`, and
  `ZLINK_PUB_OPT_NODROP` = `0`.
  A binding's examples are written against these defaults.

## Actor Dispatch Binding Contract

This section is the Actor public contract that applies to every language
binding in common. A per-language document must spell out the contract
below using names and types that fit its own language convention.

Actor dispatch is not an add-on to SPOT messaging — it's an independent
public service-layer capability. Because `SpotNode`, `Spot`, and
`StreamSocket` share ownership of lifecycle and routing, each public
type exposes its own share of the responsibility. The exact surface
placement follows the `Actor Dispatch Policy` section below.

#### Actor Id, Ref, And Lifecycle Entry Points

- An Actor id is a non-empty UTF-8 string up to 255 bytes. A NUL
  character is not allowed.
- An Actor ref carries `node_rid`, `actor_id`, and `generation`.
  `generation == 0` is an unchecked remote ref, and is not treated as an
  invalid value.
- Creating an unchecked remote Actor ref is owned by `SpotNode`. It may
  be expressed as a static method or a factory function per language
  convention, but the canonical documentation and samples are based on
  the `SpotNode`-owned surface. Do not add a separate duplicate unchecked
  factory as public API directly on `ActorRef` itself.
- A local Actor is created by `SpotNode`. An Actor can join only one Spot
  at a time, and leaving does not drain unread messages.
- `Actor.close`, or an equivalent lifecycle method, destroys the local
  Actor owned by that Actor handle. `SpotNode.destroyActor(actorRef)`, or
  an equivalent method, is a ref-based destroy surface for a caller that
  holds only an Actor ref, without an Actor handle. These are not the
  same responsibility repeated under two names — they are two entry
  points with different owners, and a per-language spec must document
  this difference.
- `Actor.join` / `Actor.leave` are the surface for a caller holding a
  local Actor handle. `SpotNode.joinActor(actorRef, ...)` /
  `SpotNode.leaveActor(actorRef, ...)` are the surface for a caller
  holding only an Actor ref. Providing only one of the two makes either
  the ref-only flow or the owned-handle flow unnecessarily complicated.

#### STREAM Session Binding

- One STREAM session can bind multiple Actors. Bind/unbind is keyed on
  the session routing id and either the actor id or the Actor ref.
- A public API that sends from STREAM to an Actor uses the bound session
  and actor id as its selector. A removed lookup/send helper name is not
  kept in the public API.
- `Actor.sendBoundSession` and `Actor.closeBoundSession` do not take a
  session routing id as an argument. An Actor hides its current
  bound-session selection internally. When a caller must select
  explicitly by session routing id, it uses
  `StreamSocket.sendBoundActor(...)` instead.
- Actor recv info's `source_node_rid` and `source_session_rid` are value
  fields of the core struct, so they are not documented as
  nullable/optional. No-data is delivered only through the recv result's
  own representation, such as `false`, a no-data result, or `Ok(false)`.

#### Dispatch And Join Results

- An Actor readable dispatch event must let the caller know which Actor
  to drain. A language that hands the callback off to a different
  execution context must non-blockingly pre-drain the Actor part at
  callback-entry time, so the public dispatch info can return that part.
- A Spot join request carries a message. A join reply must also return a
  message to the caller together with the accept/reject result. Join
  completion must deliver the final Actor ref (for a remote join, the
  target node's ref) and the joined Spot rid to the application through
  a dedicated `actor join` result type.
- The request-reply surface exposes only the payload part the core reply
  function supports. Because the core reply function has no send-flag
  argument, a binding does not add a no-op flag-setting step to the reply
  builder.

#### Removed APIs

- Remote Actor creation and the admission handler have been removed from
  the public surface. An Actor that must start on a remote node is
  created by the application directly on that SpotNode with `actor_new`.
  When a checked ref for a remote Actor is needed, use the async
  `remote_actor_get_ref` lookup.
- Actor location is updated through the Actor creation, Spot join/leave,
  and Actor destroy flows. STREAM session bind/unbind neither creates nor
  removes an Actor location.
- Session attach and Actor location movement are different state
  transitions. Joining a user Spot does not require a bound STREAM
  session. Moving an Actor's location does not automatically change the
  session mapping.
- There is no per-Actor queue-limit option. A binding must not make this
  a public option.
- A removed Actor ref function, a stream actor lookup/send helper, or a
  session-actor-key design name is not kept in the public surface or
  documentation.


## Document Interpretation Rules
- This document's policy body is a normative document by default.
- The terms below carry the following meaning.
  - `Required`: an item that must be followed in the current review and
    implementation. Non-compliance blocks the review.
  - `Recommended`: an item strongly recommended, but which can be applied
    in stages depending on the binding's characteristics. Non-compliance
    requires a reason during review but does not block it.
  - `Target`: a goal item to align to over the long term. It applies only
    once a given binding decides to implement that component. If a
    binding decides not to implement it, review does not require it.
  - `Internal-only`: an item that can be used inside a binding's
    implementation, but must not be exposed through the public API,
    samples, guides, or spec signatures.
- Absent a separate marker, treat the policy body as `Required`.
- If a section title is marked `(Target)` or `(Recommended)`, that whole
  section is interpreted at the marked level. This takes priority over
  the unmarked default (`Required`).
- The `Implementation Review Checklist` section is not a design draft for
  adding a new API — it's the standard for confirming an implementation
  follows an already-defined public API contract.
- A checklist item does not replace the semantic contract defined in the
  document body.

## Core Principles
- The core contract's single baseline is `zlink.h`'s `*_part` substrate.
- The internal implementation of the send/recv/request/reply/publish/
  subscribe family must use the core `*_part` substrate. It does not call
  an aggregate-shaped core function directly from inside a binding.
- The public API is designed around a multipart model.
- Blocking and non-blocking can be distinguished by name.
- The same capability is not exposed redundantly through multiple paths.
- Value meaning is raised into an enum, boolean, or value object, not
  left as `int`.
- A raw option bag is not exposed publicly.
- A binding does not infer core's state errors.
- A binding blocks an input value's format, range, overflow, and
  truncation risk up front.
- Structure follows POSD principles, prioritizing deep modules,
  information hiding, and low change amplification.
- This document defines the semantic contract first.
- A per-language surface can differ to fit each language's convention,
  but the semantic contract must be the same.

## Monitor Ready Contract
- The `value` of a `*_READY_CHANGED` monitor event is not an aggregate
  ready-count contract.
- A binding's public API must not assume a monitor snapshot has a
  ready-count surface.
- When a readiness gate is needed, use the low-cost event edge directly.
- Raw perf/samples use `CONNECTION_READY` event counting.
- SPOT perf/samples do not use a separate service event gate.
- SPOT perf uses an explicit `READY`/`START` barrier protocol.
- Do not turn a delivery-ready/count-family monitor event into a new gate
  contract.

## POSD Structure Policy
- Binding design follows John Ousterhout's POSD principles.
- A public API must reduce the number of concepts a user needs to know.
- Internal implementation complexity must be hidden behind a facade,
  value object, or domain object.
- Avoid a shallow wrapper.
  - Do not multiply a public wrapper that only renames a native function
    without adding new meaning.
- Do not repeatedly expose the same capability through multiple types and
  multiple names.
- Gather a rule whose change must end in one place into a single module.
  - Example: a routing-id length limit
  - Example: the send-failure contract
  - Example: typed-option ownership
- A role, owner, no-data, error, or naming rule shared across languages
  is owned exactly once by this policy document. A per-language spec does
  not redesign the same rule — it expresses this document's contract in
  that language's convention.
- If a per-language spec needs a rule that differs from this document, do
  not change the individual document first. First write the exception
  reason and scope into this policy document, then update that language
  document. This keeps the same design decision from scattering across
  multiple documents.
- Reduce temporal decomposition.
  - Example: forbid an API where a user must remember the order in which
    to combine `setOption` calls
- A public API reveals "what it can do," not "how it's wired internally."
- Treat a value object and a result object as deep modules.
  - Give the caller a small interface, while encapsulating validation,
    ownership, and shape rules together internally.

## Public Surface Rules

### Base Type Exposure
- Where possible, let a user directly use only a concrete socket type at
  compile time.
- Avoid a structure where a user directly uses a generic root base, a raw
  compat base, or a shared base instead of a concrete socket type.
- A statically typed binding must enforce this rule using public
  type/export/visibility.
- A dynamic binding must enforce the same rule with export restrictions
  and surface tests.
- A generic root base or raw compat base exposes only the common
  lifecycle and common management functionality externally.
- A role-specific shared base may expose externally only the capability
  every descendant has in common.
- A socket-type-specific role must not be promoted to a generic root base
  or a raw compat base.
- Example common functionality a public base may allow external access
  to:
  - `bind`, `unbind`
  - `connect`, `disconnect`, `disconnectRid` on connectable base only
  - `close` / `dispose`
  - common typed options
  - `monitorOpen`, or an equivalent monitor entry point
  - `setTlsServer`, `setTlsClient`, or an equivalent TLS helper
- Functionality a generic root base or raw compat base must not allow
  external access to:
  - `send(...)`
  - `send(routingId, ...)`
  - `sendParts(...)`
  - `sendFrom(...)`
  - `recv()`
  - `recv(flags)` / `recv(size, flags)`
  - `recvInto(...)`
  - `recvMsgInto(...)`
  - a routed-receive alias (`receiveRouted`, and so on)
  - `publish(...)`
  - `setSubscription(...)`
  - `unsetSubscription(...)`
  - `subscribe()`
  - `receiveSubscriptionEvent()`
  - raw direct receive handler registration
  - `onSubscribe(...)`
  - `setSendReadyHandler(...)`
  - `setRoutingId(...)`, `getRoutingId()`
  - `attachStreamRaw(...)`, `detachStream()`
  - `streamAttach(...)`, `streamAttachRaw(...)`, `streamDetach()`
  - `streamPeerRoutingId(...)`, `streamSend(...)`
  - a raw option bag (`setOption`, `getOption`, `setSockOpt`,
    `getSockOpt`, and so on)
  - a topic/socket-type-specific option facade
  - a legacy alias that bypasses the canonical name
    - example: `recvHandler(...)`, `subscribeHandler(...)`
- A role-specific shared base can allow a role only when it's common to
  every descendant.
  - example: `setSubscription`, `unsetSubscription`, `subscribe` on a
    subscriber-only base
  - example: `publish`, `setSendReadyHandler` on a publisher-only base
- The roles above must exist as public only on a concrete socket type
  marked `Y` in the role matrix.
- A base-mediated bypass call must not be possible for a socket type
  marked `—` in the role matrix.
- Perf, sample, helper, and compat layers must not treat a base entry
  that bypasses the canonical public surface rule as a new baseline
  either.
- Even when a deprecated compat API is needed, isolate it into a compat
  namespace or internal surface separate from the canonical public API.
- A structure where a user must remember a `SocketType` and a raw flag
  combination to pick the right send/recv family is treated as a POSD
  violation.

### Multipart Only
- Unify the send/receive public surface around multipart.
- Do not put a single-message-receive convenience overload in the public
  surface.
- A single-part send convenience method can be allowed.
  - example: `send(Message part)` as a convenience overload of
    `send(List<Message> parts)`
- A receive result is returned as a language-appropriate domain object or
  an equivalent multipart representation.

### Error Handling Policy

Every data-path function (`send`, `recv`, `request`, `reply`,
`subscribe`, `publish`) follows the same error-handling principle.

#### Principles

1. **A language with exceptions does not deliver an error through a
   return value.**
   - Applies to: C++, Java, .NET, Node, Python.
   - Returns a result, or void, on success.
   - Throws an exception on failure.
   - The exception carries an `int code` (in the 0–706 range) so the
     caller can distinguish the failure cause.
   - Every failure including `BACKPRESSURED`, `NOT_CONNECTED`, and
     `NOT_FOUND` is delivered as an exception. These are never return
     values.
2. **C / Go / Rust have no exceptions, so they follow a return-based
   contract.** A binding handles it in the style each language's idiom
   fits.
   - C: returns a per-function typed result enum
     (`zlink_submit_result_t`, `zlink_recv_result_t`,
      `zlink_handler_result_t`, `zlink_close_result_t`,
      `zlink_bind_result_t`, `zlink_connect_result_t`,
      `zlink_config_result_t`).
   - Go: returns `(T, error)`. The error object carries an `int` code.
   - Rust: returns `Result<T, E>`. `E` is, where possible, a concrete
     per-function-family error (`BindError`, `SubmitError`, and so on),
     promoted to `ZlinkError` only at a boundary where multiple function
     families mix. An error value carries an `int` code. Callers use the
     `?` operator to propagate.
3. **Express blocking-vs-non-blocking through `flags` and the return
   rule, instead of a `Try*` name.**
   - C keeps the C ABI functional contract.
   - Go / Rust keep return-based error delivery, but still apply the
     wrapper binding's ref-out recv and operation builder rules.
   - `.NET` / `Java` / `Node` / `Python` / `C++` do not add a public
     `trySend`, `tryRecv`, or `tryRequest`.
   - The C ABI expresses blocking vs. non-blocking through a function
     argument `flags`.
   - A wrapper binding's send/publish/request/reply family expresses a
     non-blocking submit through the builder's `.flags(...)` step. It
     does not add a separate `flags` argument to the operation
     start-point signature.
   - A wrapper binding's data-plane `recv`, routed recv, and `subscribe`
     fill caller-provided result storage, and the return value expresses
     only "was data received."
   - When a non-blocking receive currently has no data, it returns a
     per-language no-data representation such as `false`, `nil, false`,
     or `Ok(false)`, and only a real error is delivered as an exception
     or a return error.
   - An async request is selected through the same `request` operation
     builder's completion-object-return step, and it does not take
     submit flags.
   - A transport-style name such as `sendNoWait`, `recvNoWait`, or
     `publishNoWait` does not belong on the public surface.
4. **Looking up `INTERNAL_ERROR` detail.**
   - When the result code is in the `INTERNAL_ERROR` family (12, 105,
     206, 306, 404, 505, 604, 704, and so on), the internal raw errno can
     be looked up with `zlink_errno()`.
   - The binding's error type (an exception object for exception
     languages, an error value for return-based languages) exposes this
     through an `internalErrno`/`internal_errno` field (for debugging
     only).
   - For every other result code, calling `zlink_errno()` is
     unnecessary.

#### Per-Language Error Representation

| Language | Handling | Error type | Code access | Internal errno |
|---|---|---|---|---|
| C | return | per-function result enum | the enum value itself | `zlink_errno()` |
| C++ | return / throw | caller-provided recv returns `int`; other failures use `zlink_error_t` | recv: the return value; exception: `.code()` | recv: `errno` when `-1`; exception: `.internal_errno()` |
| Java | throw | `ZlinkException` | `.getCode()` | `.getInternalErrno()` |
| .NET | throw | `ZlinkException` | `.Code` | `.InternalErrno` |
| Go | return | `error` | `.Code()` | `.InternalErrno()` |
| Rust | return (`Result`) | `ZlinkError` | `.code()` | `.internal_errno()` |
| Node | throw | `ZlinkError` | `.code` | `.internalErrno` |
| Python | throw | `ZlinkError` | `.code` | `.internal_errno` |

- The `return` group (C / Go / Rust) has the caller explicitly check the
  return value. Go uses `if err != nil`; Rust uses the `match`/`?`
  operator idiom.
- The `throw` group (C++ / Java / .NET / Node / Python) propagates an
  exception. The caller handles it with a per-language `try`/`catch` or
  by propagating further up.

#### Error Codes

- The C API returns a per-function typed result enum.
- Every enum value is unique across the 0–706 range.
- A binding includes this code in its per-language error type's `int
  code` (an exception object for exception languages, a return error
  value for return-based languages).
- For the full enum definition, see
  [errno-map.md](https://kairos-code-dev.github.io/zlink/en/spec/core/04-errno-map/).

#### Per-Function Error Type Hierarchy

**Every binding inherits the C API's per-function typed-result-enum
structure as-is.** With only a single `ZlinkException`/`ZlinkError`, a
caller could not know the set of possible errors from the signature
alone.

Each binding provides 8 function-family error types as subtypes of
`ZlinkException`/`ZlinkError`. A method signature must expose that
function family's concrete error type.

| C result enum | Function family | Subtype (semantic contract) |
|--------------|--------|--------------------------|
| `zlink_submit_result_t` | send / publish / request submit / reply submit | `SubmitError` |
| `zlink_request_result_t` | request completion (callback) | `RequestError` |
| `zlink_recv_result_t` | recv / subscribe / subscription event / monitor recv / timer recv | `RecvError` |
| `zlink_handler_result_t` | handler registration | `HandlerError` |
| `zlink_close_result_t` | close / destroy | `CloseError` |
| `zlink_bind_result_t` | bind | `BindError` |
| `zlink_connect_result_t` | connect / disconnect / unbind | `ConnectError` |
| `zlink_config_result_t` | option set/get, snapshot, poller mutation, proxy, timer config | `ConfigError` |

##### Per-Language Naming

| Language | Top-level type | Subtype naming | Base type | Example signature |
|------|-----------|----------------|----------|-------------|
| C | — | the per-function typed enum as-is | — | `zlink_bind_result_t zlink_bind(...)` |
| C++ | `zlink_error_t` | `zlink::<category>_error_t` (snake_case + `_t`) | the `std::runtime_error` family | `void bind(...) /* @throws bind_error_t */` |
| Java | `ZlinkException` | `<Category>Exception` | **unchecked** (`RuntimeException`) | `void bind(...) /* @throws BindException */` |
| .NET | `ZlinkException` | `Zlink<Category>Exception` | `System.Exception` (unchecked; every .NET exception is unchecked) | `void Bind(...) /* throws ZlinkBindException */` |
| Node | `ZlinkError` | `<Category>Error` | `Error` | `bind(ep): void /* @throws BindError */` |
| Python | `ZlinkError` | `<Category>Error` | `Exception` | `def bind(ep): ...  # raises BindError` |
| Go | `error` (interface) | `*<Category>Error` (typed error struct) | implements the `error` interface | `func (s) Bind(ep) error  // returns *BindError` |
| Rust | `ZlinkError` (enum) | `<Category>Error` (a variant or a separate type) | implements `std::error::Error` | `fn bind(ep) -> Result<(), BindError>` |

- `Category` is one of 8: `Submit`/`Request`/`Recv`/`Handler`/`Close`/
  `Bind`/`Connect`/`Config`.
- `ZlinkException`/`ZlinkError` stays the parent of every subtype,
  preserving the "catch-all" idiom. A caller catches the subtype when it
  needs granularity, or the parent otherwise.
- Each subtype error has its own dedicated `ErrorCode` nested enum for
  that function family. Another function family's codes are not
  expressed in that type.
- **Java / .NET follow the unchecked-exception system.** A method
  signature does not force a `throws` clause. A possible exception is
  documented with Javadoc `@throws` / XML doc
  `/// <exception cref="...">`.
- Rust / Go declare the concrete subtype error as the return type. A
  dynamic language (Node/Python) provides the same information with
  TSDoc `@throws` / a Python docstring `Raises:`.

##### Signature Declaration Rules

- When a method can throw/return only a single function family's error,
  declare only that concrete subtype.
  - Java: `@throws BindException` (Javadoc; no `throws` clause needed in
    the signature)
  - .NET: `/// <exception cref="ZlinkBindException">`
  - C++: `/// @throws bind_error_t` (do not mark it `noexcept`)
  - Node: TSDoc `@throws {BindError}`
  - Python: docstring `Raises: BindError`
  - Go: document the return type as `returns *BindError`
  - Rust: return type `Result<T, BindError>`
- When a method spans multiple function families (for example, a service
  layer's combined call), declare the shared parent
  `ZlinkException`/`ZlinkError` and list the subtypes that can actually
  occur in the doc.
- A validation exception (a language-native `IllegalArgumentException`,
  and so on) is separate from the system above and does not enter the
  `ZlinkException`/`ZlinkError` hierarchy.

### Flags Policy

Every data-path function has a `flags` option. An ordinary socket
function expresses it as a per-language signature's `flags` parameter,
and a function targeting a SPOT operation builder expresses it as the
builder's `flags(...)` step.

| Function family | `flags` usage |
|---|---|
| `send`, `publish`, `reply` | `DONTWAIT` — non-blocking submit |
| `recv`, `subscribe`, `receiveSubscriptionEvent` | `DONTWAIT` — non-blocking receive |
| `request` (callback) | `DONTWAIT` — non-blocking submit |
| `request` (async completion) | No flags — uses the per-language completion-object-return path |

- The default `flags` value is `0` (blocking).
- A non-blocking call's temporary state is delivered per each language's
  public contract.
  - `.NET` / `Java` / `Node` / `Python`
    - `send`, `publish`, callback `request`: `false` on temporary
      backpressure
    - caller-provided `recv`, `subscribe`,
      `receiveSubscriptionEvent`: `false` when there is currently no
      data
    - Any other failure: a typed exception
  - C++
    - operation builder `send` / `publish` / callback `request`: `false`
      on temporary backpressure
    - caller-provided `recv` / `subscribe` /
      `receive_subscription_event`: returns the `recv_result_t::no_data`
      integer value when there is currently no data
    - only a binding-local failure returns `-1` and sets `errno`
  - Return-based languages (C/Go/Rust): return the error (C = result
    enum, Go = `error`, Rust = `Err(E)`).
- Per-language `flags` representation:
  - C: `int flags = 0` (the C ABI does not apply the builder policy)
  - C++ / Java / .NET / Node / Python / Go / Rust send/request/reply/
    publish/Actor-attach surfaces: expressed through the builder's
    `.flags(...)` step. No separate `flags` argument or `_with_flags`
    variant is added to the operation start-point signature.
  - C++ / Java / .NET / Node / Python / Go / Rust data-plane
    recv/subscribe surfaces: take a `flags` argument together with
    caller-provided output storage.

### Naming Policy

#### Creation Function Naming

A public function that builds a new object from an input value, such as
`Message` and `RoutingId`, aligns to the `.NET` binding's `From(...)`
meaning. Avoid putting the input type into the function name — doing so
splits the same concept into multiple names across languages.

- Gather ordinary construction under a single `from(...)`, or that
  language's equivalent name. A type-suffixed name such as `from_bytes`,
  `from_string`, `from_u32`, or `from_uuid` is not used as canonical
  public API.
- Hex decoding is allowed as an exception, under the `from_hex` family,
  because it carries a distinct meaning — decoding a human-readable
  string. The per-language spelling follows idiom: `FromHex`, `fromHex`,
  `from_hex`, `NewRoutingIDFromHex`.
- Python uses `from_(...)` because `from` is a reserved word.
- Rust uses the standard `From` implementation for a routing id, and can
  use the `try_from` idiom for a message construction that can fail. It
  does not add an input-type-named public helper such as `from_bytes`/
  `from_string`.
- Because Go has no overloads, it allows a typed constructor such as
  `NewRoutingID(...)`, `NewRoutingIDString(...)`,
  `NewRoutingIDUint32(...)`, and `NewRoutingIDUUIDBytes(...)`. This
  exception exists to preserve Go's static-typing style, and it does not
  repeat both `From` and the type name together, as in
  `NewRoutingIDFromString`.
- Allocation is not a source conversion, so it uses `allocate(...)` or a
  per-language constructor idiom (`NewMessageWithSize(...)`, and so on).

#### One Entrypoint, Variation Expressed As Builder Steps

Variations of the same operation — async/callback, single/multipart,
with or without flags, with or without a timeout — use the same
entrypoint name, and the variation is expressed as a builder step. Do not
create a separate name such as `request_callback`, `send_nonblocking`, or
`send_with_flags`.

```
// GOOD: one name, builder absorbs the form.
spot.request_to_channel(channel)
    .message(part)
    .timeout(Duration::from_secs(3))
    .submit()                              // returns the language completion object

spot.request_to_channel(channel)
    .message(part)
    .flags(SendFlags::DONTWAIT)
    .submit(callback)                      // callback variant

// BAD: split names for the same operation.
request_to_channel(channel, parts, timeout)
request_to_channel_callback(channel, parts, callback, flags, timeout)
```

#### Shared Result Type Names

A shared result type does not repeat its owner's name. The type name
reveals the domain concept the value represents, directly.

- The canonical name for a poller wait-result type is `PollEvent`. C++
  uses `poll_event_t`; Java/.NET use `PollEvent`; Node/TypeScript use
  `PollEvent`. A name that appends the owner a second time, like
  `PollerEvent`, is not used as canonical public API.
- Timer, monitor, and dispatch results follow the same rule. When the
  owner is already clear from the return type or namespace, the type
  name does not repeat it.

#### SPOT Target Naming

SPOT routed naming separates pub/sub from targeted messaging.

- **Channel-aware path**
  - `send_to_channel(channel_name) -> SendOp`
  - `request_to_channel(channel_name) -> RequestOp`
- **SPOT topic path**
  - `publish(topic) -> SendOp`
    - The receiver is already a publish-capable socket or `Spot`, so it
      does not repeat owner or parameter meaning, as in `publish_spot`
      or `publish_to_topic`.
- **Direct routed path**
  - `send_to_spot(dest_node_rid, dest_spot_rid) -> SendOp`
  - `request_to_spot(dest_node_rid, dest_spot_rid) -> RequestOp`
  - `request_to_router(peer_rid) -> RequestOp`
- **Reply path**
  - `reply_to_spot(dest_node_rid, dest_spot_rid, request_seq) -> ReplyOp`
  - `reply_to_router(peer_rid, request_seq) -> ReplyOp`

The payload and options of `SendOp`, `RequestOp`, and `ReplyOp` are
expressed through the `message(...)`, `flags(...)`, `timeout(...)`, and
`submit...` steps the `Operation Builder Policy` section defines. So a
new canonical SPOT surface does not add a `Message`/`List<Message>`/
`flags`/`timeout` combination overload on the same start point.

On a new SPOT binding surface, `send_to_channel`/`request_to_channel`/
`publish(...)` are treated as the default path, instead of the old
`send_service`/`request_service`. A direct address-targeted path can be
separately supported as core's typed routed surface.

Convert to camelCase / PascalCase / snake_case per each language's
convention.

### Request Policy

A request can offer both a per-language async-completion form and a
callback-completion form, and both are selected at the submit step of
the same `RequestOp` operation builder the `request` entrypoint returns.
Do not create a separate name (`request_callback`, `requestAsync`, and
so on).

For a SPOT operation builder target, the work start point is
`requestToChannel`/`requestToSpot`/`requestToRouter`; for a raw
`DealerSocket`/`RouterSocket`, the work start point is
`request`/`request(peer)`. Regardless of the start point, the completion
mode is selected through the per-language final execution method the
[bindings async execution surface policy](async-coroutine-policy.md)
defines.
- **On success, it returns only the reply payload's `List<Message>`.**
  The caller already knows the `routing_id` and `request_seq` of the
  request it sent, so it does not need `Received` back. A separate
  `Reply` type is not created.
- Because multipart reply is possible, it returns `List<Message>`, not a
  single `Message`. A single-part reply is retrieved with `list[0]`.

#### Callback Request

The builder's callback submit method (`submit(callback)`).

- Takes a flags parameter. Delivered through the builder's `.flags(...)`
  step; a non-blocking submit is possible with `DONTWAIT`.
- The timeout is delivered through the builder's `.timeout(...)` step. If
  not specified, it uses the socket's default timeout.
- The submit step is interpreted as follows.
  - Exception-based languages: blocking success = `true`, non-blocking
    temporary backpressure = `false`, any other submit failure = an
    exception
  - Return-based languages: keeps the existing error-return contract
  On failure, the callback is not registered.
- On submit success, the callback is called exactly once.
  - Success: `result = OK`, includes reply parts
  - Failure: `result != OK` (`TIMED_OUT`, and so on), parts is
    empty/null/None/`Option::None`
- The callback signature follows language idiom, and **delivers the
  reply payload as `List<Message>`** (not `Received`):
  - The common pattern (C++/Java/.NET/Node/Python/Go):
    `(RequestResult result, List<Message> parts)` — a result enum and a
    parts list
  - Rust idiom: `FnOnce(Result<Vec<Message>, RequestError>)` — this
    pattern is allowed because `Result` is Rust's standard way to
    express an error plus a value. `RequestError::code` maps 1:1 to the
    `RequestResult` enum value.

#### Shared

- For the full `zlink_request_result_t` definition, see
  [errno-map.md](https://kairos-code-dev.github.io/zlink/en/spec/core/04-errno-map/).
- Because Go / Rust have no exceptions, a callback request's submit
  failure is also handled in a return-based way (Go: returns
  `*SubmitError`; Rust: returns `Result<_, SubmitError>`).

## Domain Object Policy
- Java, C#, Go, Rust, Node, and Python prefer a domain object over an
  `out` parameter or a raw tuple where possible.
- The minimum core domain model:
  - `Message`
  - `RoutingId`
  - `Received`
  - `TopicMessage`
  - `SubscriptionEvent`
  - `SubmitResult` (C / Go / Rust — included in the return object/error
    for return-based languages; exposed as the exception object's
    `.code` for exception languages)
- A result object must describe payload shape, ownership, and optional
  routing metadata together.
- A convenience feature is a method on the result object.
  - example: `singlePartOrThrow()`

### Domain Object Canonical Shape (Shared By Every Binding)

Every domain object exposes the canonical field/method set below **as
is**. Only the naming convention (camelCase / snake_case / PascalCase)
is converted per language — **the field type and method meaning do not
change.** A per-language idiomatic convenience method can be added, but
it must not replace or partially omit a canonical method.

#### `Message`

A single message part that carries a transport payload. Every send/
request/reply/publish builder accumulates one or more `Message`s to
build a multipart payload.

| Member | Type | Meaning |
|------|------|------|
| empty constructor | ctor/static | Creates a zero-length message |
| `allocate(size)` | static/ctor | Creates a `size`-byte payload buffer |
| `from(bytes)` | static/ctor | Copies bytes-like input into message-owned storage |
| `from(string)` | static/ctor | Encodes a user string as a UTF-8 payload |
| `copy()` / `from(Message)` | `Message` | Copies the source payload into a new message |
| `move()` / consume path | `Message` / builder step | An explicit ownership transfer; the source cannot be reused after the call |
| `size` | `int` / `usize` | Payload byte length |
| `is_empty()` | `bool` | `size == 0` |
| `to_bytes()` | `bytes` / `byte[]` / `Vec<u8>` | A snapshot copy of the payload |
| `data` / `as_bytes()` | view | A read view of the payload; no lifetime guarantee after close |
| `mutable_data` / `as_mut_bytes()` | mutable view | A mutable view for filling an allocated payload |
| `copy_to(destination)` | `int` / `bool` | Copies the payload into a caller-provided buffer |
| `to_string()` / `as_str()` | `string` / result | A UTF-8 decode convenience |
| `get_property(name)` | `string?` / result | Looks up a native message string property |
| `ref_count()` | `int` | A native-storage reference-count diagnostic value |
| `close()` / `Dispose()` / `Drop` | — | Cleans up native storage, per each language's lifecycle idiom |

The name follows per-language idiom. The meaning fits the slots below.

| Meaning | .NET | Java | Node | Python | Rust | C++ | Go |
|------|------|------|------|--------|------|-----|----|
| Empty message | `new Message()` | `new Message()` | `Message.from(Buffer.alloc(0))` or equivalent | `Message()` | `Message::new()` | `message_t()` | `NewMessage(nil)` |
| Sized allocation | `Allocate(size)` | `allocate(size)` | `allocate(size)` | `allocate(size)` | `with_size(size)` / `allocate(size)` | `allocate(size)` | `NewMessageWithSize(size)` |
| Bytes copy | `From(bytes)` | `from(byte[])` | `from(BufferLike)` | `from_(buffer)` | `try_from(bytes)` | `from(...)` | `NewMessage(data)` |
| UTF-8 string | `From(string)` | `from(String)` | `from(string)` | `from_(str)` | `TryFrom<&str>` or equivalent | `from(std::string)` | `NewMessageString` |
| External buffer copy | — | `from(ByteBuffer)` / `from(ByteBuf)` | `from(BufferLike)` | `from_(buffer)` | `try_from(bytes)` | `from(...)` | `NewMessage(data)` |
| Message copy | `Copy()` | `from(Message)` | `copy()` or `from(Message)` | `copy()` | `Clone` or `try_clone()` | copy constructor | `Clone()` / `Copy()` |
| Explicit move | `Move()` / `MoveMessage(...)` | `move()` / `moveMessage(...)` | `moveMessage(...)` | `move_message(...)` | move-by-value | move constructor / rvalue builder | `MoveMessage(...)` |
| Bytes snapshot | `ToArray()` | `toByteArray()` | `toBytes()` | `to_bytes()` | `to_vec()` | `to_bytes()` | `BytesCopy()` or equivalent |
| Read view | `AsReadOnlySpan()` | `dataBuffer()` | `data()` | `data` | `as_bytes()` | `bytes()` | `Data()` |
| Mutable view | `AsSpan()` | `mutableDataBuffer()` | `data()` | `data` | `data_mut()` | `bytes()` / `data()` | `Data()` |
| UTF-8 decode | `GetString()` | `toUtf8String()` | `toString()` / `getString()` | `to_string()` / `decode` helper | `as_str()` | `to_string()` | `String()` / `Text()` |
| Property | `GetProperty(name)` | `getProperty(name)` | `getProperty(name)` | `get_property(name)` | `get_property(name)` | `property(name)` | `GetProperty(name)` |
| Refcount | `RefCount` | `refCount()` | `refCount()` | `ref_count()` | `ref_count()` | `ref_count()` | `RefCount()` |

Rules:
- The `from(bytes)` family always copies into message-owned storage. The
  caller must be free to change or release the input buffer afterward.
- Java's `from(ByteBuf)` copies a Netty `ByteBuf`'s readable bytes
  without changing `readerIndex`. `copyTo(ByteBuf)` writes into the
  destination's writable region and advances `writerIndex`.
- A borrowed/zero-copy constructor is not part of the canonical public
  contract. Even if a particular binding uses one as an internal
  optimization, the public API must not push lifetime responsibility
  onto the caller.
- The `message(...)` builder step follows the original-preservation
  contract. The caller must be able to reuse the message it passed even
  after a submit failure.
- Ownership transfer is allowed only through a separate path whose name
  reveals consume semantics — `move`, `MoveMessage`, move-by-value. This
  path must document that the original message cannot be reused even
  after a submit failure.
- `to_bytes()` is a snapshot copy. Allocation-free payload access is kept
  separate as a read-view API (`data`, `as_bytes`, `AsReadOnlySpan`, and
  so on).
- A read/mutable view is valid only until the message is closed,
  disposed, or dropped. A binding does not guarantee view usage after
  close.
- `get_property(name)` is a diagnostic/interop API for reading native
  message metadata. A property-write API is not part of the shared
  required contract.
- `ref_count()` is a diagnostic value. Do not build a public contract
  that judges ownership policy or send eligibility from the reference
  count.
- An RAII language (C++, Rust) does not have to explicitly expose
  `close()`. An explicit-lifecycle language (.NET, Java, Python, Go)
  must provide an idempotent close/dispose.
- The behavior of `size`, `data`, and `get_property` on a closed or
  moved-from message follows per-language convention, but must document
  whether it returns an empty value or throws an exception/error.

#### `TopicMessage`

The recv result for raw `SUB`/`XSUB` and `Spot subscribe`. Raw pub/sub
wraps C API `zlink_subscribe_part()`, and Spot subscribe wraps
`zlink_spot_subscribe_part()`, into a single binding domain object. The
binding's public API assembles the part-helper call result into a
per-language multipart object and returns it.

| Member | Type | Meaning |
|------|------|------|
| `routing_id` | `RoutingId?` (optional) | The sender's routing id; null/None/empty if the transport doesn't carry one |
| `topic` | **`string` (UTF-8)** | The matched topic. **Not bytes.** |
| `parts` | `List<Message>` / `Vec<Message>` | The multipart payload |
| `is_single_part()` | `bool` | `parts.size() == 1` |
| `first_part()` | `Message` | `parts[0]`; error/exception if empty |
| `single_part_or_throw()` | `Message` | Returns the part if `is_single_part()`, else error/exception |
| `close()` / `Dispose()` / `Drop` | — | Cleans up held parts, per each language's lifecycle idiom |

Rules:
- Do not create `Subscribed` or a similar subclass. Expose only
  `TopicMessage`.
- A Spot subscribe result exposes `topic + parts` together. The channel
  is state that already lives on the `SpotNode` the `Spot` handle is
  bound to, so it is not repeated as a message result field.
- `topic` is a UTF-8 `string`. It is not exposed as `bytes`/`byte[]`/
  `Vec<u8>` (even if it arrives internally as raw bytes, the public API
  decodes it).
- Keep only a single typed `RoutingId` field. Do not create a dual
  property such as `RoutingId: string` plus `RoutingIdValue: RoutingId?`.

#### `Received`

The single canonical domain object that carries a PAIR / DEALER / ROUTER
/ STREAM / SPOT routed recv result. Other than lacking a topic field, it
has the same convenience-method set as `TopicMessage`. A routed recv
result provides a `send()` operation builder for sending an ordinary
response, and a request-reply result also provides a `reply()` builder.
Both entrypoints accumulate payload and options through builder steps,
per the `Operation Builder Policy`.

`Received` is not a per-socket-kind message wrapper. Request meaning is
the same across DEALER, ROUTER, and SPOT, and is expressed only through
`request_seq` and reply context. A binding must not add a
protocol-specific public result type such as `DealerReceived`/
`RouterReceived`/`SpotReceived` as a new canonical surface. If an
existing binding has such a type, remove it, and new code, samples,
perf, and framework integrations must use `Received`.

| Member | Type | Meaning |
|------|------|------|
| `routing_id` | `RoutingId?` | The sender's routing id (router = `peer_rid`, spot = `source_node_rid`) |
| `spot_rid` | `RoutingId?` | Set only for SPOT routed recv (`source_spot_rid`) |
| `request_seq` | `uint64?` | Set in request-reply mode; otherwise null |
| `parts` | `List<Message>` | The multipart payload |
| `is_single_part()` | `bool` | Same as above |
| `first_part()` | `Message` | Same as above |
| `single_part_or_throw()` | `Message` | Same as above |
| `send()` | `SendOp` | An operation builder that sends an ordinary routed message back to this `Received`'s sender; `SubmitError` at submit time if there's no routed source context |
| `reply()` | `ReplyOp` | A reply operation builder valid only when this was a request; `SubmitError` at submit time if `request_seq` is absent or the reply context is invalid |
| `close()` / equivalent | — | Same as above |

In .NET, `Received.Create()` is the canonical construction path for
caller-provided recv storage. `Received` stays a public concrete
contract type.

`request_seq` rules:
- `null`/`None`/an empty `Optional`/`hasRequestSeq == false` means an
  ordinary receive result.
- `0` is not exposed as "has a request" on the public high-level
  `Received`. A high-level binding converts a core out-param's
  `request_seq == 0` into absent.
- A non-zero `request_seq` means a receive result that has request-reply
  context. This meaning is the same across DEALER / ROUTER / SPOT.
- A substrate-level distinction such as a request/reply message type must
  not split the public `Received` meaning. If such a value is genuinely
  needed as a public contract, expose it only as `Received`'s shared
  metadata, not as a protocol-specific result type.

`send()` rules:
- Independent of whether it was a request. It can be called as long as
  routed source context exists, even without `request_seq`.
- `send()` has no request-reply meaning. It simply sends an ordinary
  routed message back toward whoever sent this `Received`.
- A `ROUTER` and `STREAM` receive result sends by peer routing id. A
  `SPOT` routed receive result sends by source node rid and source spot
  rid.
- Payload accumulation and options such as `flags(...)` are expressed
  through `SendOp` builder steps, and a non-blocking submit flag such as
  `DONTWAIT` is also delivered through the builder's `.flags(...)` step.

`reply()` rules:
- **Calling it is forbidden when `request_seq` is `null`.** Calling it
  anyway is handled as a `SubmitError`-family failure at the builder's
  submit step. An invalid reply context — `request_seq == 0`, an invalid
  `(routing_id, request_seq)` combination, and so on — is treated as the
  same submit domain.
- `Received` internally holds a reference to the source socket (injected
  by the binding when it builds `Received` inside recv/handler).
- Calling `reply().submit()` after the socket has closed returns
  `SubmitError(TERMINATED)`.
- The server-side user does not need to separately store
  `(peerRid, requestSeq)` — `Received` alone is self-contained.
- A separate `router.reply(peerRid, seq).message(...).submit()` path is
  also kept for pull-mode compatibility, but **the recommended path is
  `received.reply().message(...).submit()`**.

#### `SubscriptionEvent`

The subscribe/unsubscribe event XPub receives, and the recv result for a
Spot subscription event.

| Member | Type | Meaning |
|------|------|------|
| `routing_id` | `RoutingId?` | The subscriber's routing id |
| `topic` | `string` (UTF-8) | The subscribed/unsubscribed topic |
| `subscribed` | `bool` | true = subscribe, false = unsubscribe |

Rules:
- Expose it only as a value object (no methods, fields only).
- No lifecycle such as `close()` (it's a value type).
- A Spot subscription event result exposes `topic + subscribed`.

#### `RoutingId`

A routing-id value object. Binary-safe (1–255 bytes).

| Member | Type | Meaning |
|------|------|------|
| `bytes` / `data` | `bytes` / `byte[]` / `Vec<u8>` / `Buffer` | The raw bytes (an immutable view) |
| `size` | `int` (1–255) | The byte length |
| `from(bytes)` | static/ctor | Builds from raw bytes |
| `from(value: string)` | static/ctor | Encodes a user string as UTF-8 bytes |
| `from_hex(value)` | static/ctor | Rebuilds from a hex string produced by `to_hex()` |
| `from(value: uint32)` | static/ctor | Builds a 4-byte big-endian `uint32` routing id |
| `from(value: guid)` | static/ctor | Builds a 16-byte UUID routing id |
| `to_bytes()` | `bytes` | Returns the original bytes |
| `to_hex()` | `string` | Displays the raw bytes as a hex string |
| equality / hash | — | Per-language idiom (`equals`/`hashCode`, `__eq__`/`__hash__`, `PartialEq+Eq+Hash`) |

The name follows per-language idiom. The meaning fits the slots below.

| Meaning | .NET | Java | Node | Python | Rust | C++ | Go |
|------|------|------|------|--------|------|-----|----|
| User string | `From(string)` | `from(String)` | `from(string)` | `from_(str)` | `From<&str>` | `from(std::string)` | `NewRoutingIDString` |
| Raw bytes | `From(bytes)` | `from(byte[])` | `from(Buffer)` | `from_(bytes)` | `From<&[u8]>` | `from(bytes)` | `NewRoutingID` |
| Hex round-trip | `FromHex` | `fromHex` | `fromHex` | `from_hex` | `from_hex` / `try_from_hex` | `from_hex` | `NewRoutingIDFromHex` |
| uint32 | `From(uint)` | `from(long)` | `from(number)` | `from_(int)` | `From<u32>` | `from(uint32_t)` | `NewRoutingIDUint32` |
| UUID | `From(Guid)` | `from(UUID)` | 16-byte `from(Buffer)` | `from_(uuid.UUID)` | `From<[u8; 16]>` | `from(std::array<uint8_t, 16>)` | `NewRoutingIDUUIDBytes` |

Rules:
- **A binary-safe value type.** Because a user-set routing id is usually
  a human-readable string, the string overload `from(value)` means UTF-8
  encoding. Arbitrary bytes received from native/core are preserved
  through the bytes overload `from(bytes)`.
- A `from_hex(value)` input allows only hex characters. A hex string is
  at most 510 characters, and it must fail with a per-language exception
  or error code if the decoded routing id exceeds 255 bytes.
- A value core treats as `uint32_t`, such as a 4-byte STREAM routing id,
  is handled through a typed API such as `from(value: uint32)`/
  `try_to_uint32(out value)`.
- A 16-byte UUID value is handled through a typed API such as
  `from(value: guid)`/`try_to_guid(out value)`.
- `to_string()`/`String()` is a per-language display string. It's
  recommended to show printable UTF-8 as-is, a 4-byte `uint32` as a
  numeric string, a 16-byte UUID as a UUID string, and anything else as a
  hex display prefixed with `hex:`. Use `to_hex()`/`from_hex(value)` for
  round-trip storage.
- Immutable. Once created, its content cannot change.
- Caching is not an observable contract. A binding can internally use a
  hash, a native struct, or a short-lived cache on the recv hot path if
  needed, but equality must always be judged by the bytes value, and a
  cache hit must not change API behavior.
- On Node, expose the `RoutingId` wrapper type as-is instead of a raw
  `Buffer`.

#### `MonitorEvent`

An event a socket monitor emits. **Required for every binding to
expose.**

| Member | Type | Meaning |
|------|------|------|
| `event` | `MonitorEventType` (enum) | The event kind (CONNECTION_READY, CONNECTED, DISCONNECTED, and so on) |
| `value` | `uint32` | A per-event detail value (for example, the reason code on DISCONNECTED) |
| `routing_id` | `RoutingId?` | The matching peer routing id (null for an event without one) |
| `local_addr` | `string` | The local endpoint |
| `remote_addr` | `string` | The remote endpoint |

#### `MonitorStatus`

The runtime status snapshot a socket monitor provides. **Required for
every binding to expose.**

| Member | Type | Meaning |
|------|------|------|
| `source_kind` | enum | The kind of monitored target |
| `state_flags` | enum flags | The state bitmask |
| `detail_flags` | enum flags | The detail bitmask |
| `snd_pending_msgs` | `uint64` | The number of messages pending in the send queue |
| `rcv_pending_msgs` | `uint64` | The number of messages pending in the receive queue |
| `auto_hwm_*` diagnostic fields | enum / number / bigint | Must expose the canonical auto-HWM fields of C's `zlink_monitor_status_t` with the same meaning. Includes enabled, profile (enum), role, policy class, unit budget, size cap, socket message slots, effective message bytes, applied HWM, applied buffer, the recent recalculation reason (enum), deferred shrink, and blocked ratio |
| `is_ready()` | `bool` | A convenience method that checks the ready bit in `state_flags`, for a raw socket monitor source only |

#### Service-Layer Entry Objects

The following are value objects returned from service-layer snapshot/
query calls. Every binding must **spell out the field list in its
spec** (a raw C struct must not be exposed as-is — wrap it in
per-language named fields).

- `SpotNodeStatus` — a spot node status snapshot
- `SpotNodePeerEntry` — a spot node peer entry. Must include `weight`.
- `SpotNodeSubjectEntry` — a spot node subject entry

Each spec spells out these types' fields as a table or code block. `C++`
wraps the raw `zlink_*_t` struct as `class <name>_t { ... }` rather than
exposing it directly on the binding API surface.

An extra method/field beyond the canonical set above is a policy
violation. When a per-language spec is found missing one, fill it in
against the canonical baseline, and remove any added non-standard
method.

## Socket Type Capability Policy
- Expose a per-socket-type capability only on that type itself.
- An unrelated socket must not have access to an unrelated function.
  - example: no publish/subscribe/xpub control surface on `PairSocket`
  - example: no general connect surface on `StreamSocket`
- Also expose a per-socket-type option only through that type's own role
  facade.

### Socket Class Naming/Structure Rule (Important)
- **A socket class name follows the core C API's socket-type name
  as-is**: `PairSocket`, `PubSocket`, `SubSocket`, `XPubSocket`,
  `XSubSocket`, `DealerSocket`, `RouterSocket`, `StreamSocket`. A binding
  must not rename it arbitrarily or add a synonym (`ClientSocket`,
  `BrokerSocket`, and so on).
- **A socket's capability functions (`send`, `recv`, `request`, `reply`,
  `publish`, `subscribe`, `on*` handlers, and so on) are exposed directly
  as methods on the socket class.** Do not create a separate wrapper/
  "helper" class for a single function or a narrow role such as
  request-reply (`RequestDealer`, `RequestRouter`, `DealerClient`,
  `RouterRequester`, and so on).
  - Reason 1: the C API's contract places
    `zlink_dealer_request_part()`/`zlink_router_request_part()`/
    `zlink_router_reply_part()` directly on the raw socket handle. The
    binding surface must preserve this structure to keep the core ↔
    binding mapping 1:1.
  - Reason 2: a wrapper class creates a duplicate lifecycle — "having to
    carry around another wrapped socket."
  - Reason 3: the role is easy to misread as inverted from the name
    (`RequestDealer` can be misread as "dealing requests").
- Keep implementation state such as a future/promise completion
  linkage (a pending map, and so on) inside the socket class, and expose
  only methods externally.
- The only exception is a service-layer surface that **combines
  different socket types** — these are independent service contracts,
  not a single-socket-function wrapper.
- This rule applies identically across every binding
  (C++/Java/.NET/Node/Python/Go/Rust), and a violation found in a spec
  file is **fixed immediately**.

### Socket Capability Matrix
- This table defines the capability each socket type must have, based on
  the `core/include/zlink.h` C API.
- This table is a public capability contract shared across every
  language binding. Functionality must not differ between bindings.
- A per-language difference is allowed only in how the same capability is
  expressed for that language's convention — casing, overloads,
  nullable, exception/error representation.
- Every binding treats this table as the answer key when writing surface
  tests.
- `Y` means every binding must expose that capability as public API.
- `—` means no binding may expose that capability as public API.

#### Connection Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `bind` | Y | Y | Y | Y | Y | Y | Y | Y |
| `unbind` | Y | Y | Y | Y | Y | Y | Y | Y |
| `connect` | Y | Y | Y | Y | Y | Y | Y | — |
| `disconnect` | Y | Y | Y | Y | Y | Y | Y | — |
| `disconnectRid` | Y | Y | Y | Y | Y | Y | Y | — |

`disconnectRid` is the peer-rid disconnect surface of a connectable raw
socket. `STREAM` is a bind-only socket and does not expose `connect`,
`disconnect`, or `disconnectRid` as public API. `Spot` also does not
expose a raw peer-rid disconnect — disconnecting a SPOT node peer is
handled by the `SpotNode.disconnectPeerRid` family.

#### Send Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `send` | Y | Y | — | — | — | — | — | — |
| `send(routingId)` | — | — | Y | — | — | — | — | Y |
| `publish` | — | — | — | Y | — | Y | — | — |

#### Receive Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `recv` | Y | Y | Y | — | — | — | — | Y |
| `subscribe` | — | — | — | — | Y | — | Y | — |
| `receiveSubscriptionEvent` | — | — | — | — | — | Y | — | — |

#### Subscription Management

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `setSubscription` | — | — | — | — | Y | — | Y | — |
| `unsetSubscription` | — | — | — | — | Y | — | Y | — |

#### Callback Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `setPacketHandler` | — | — | — | — | — | — | — | Y |
| `onReceive` | — | — | — | — | — | — | — | — |
| `onSubscribe` | — | — | — | — | — | — | — | — |
| `setSendReadyHandler` | Y | Y | Y | Y | — | Y | — | Y |

The `STREAM` public surface must provide `recv` and `setPacketHandler`.
The raw direct callback `onReceive` is not canonical public binding API.
Attempting to attach a different receive mode while one is already
active returns `HandlerResult::BUSY` (or the equivalent `EBUSY`). A
public `detachStream`/`streamDetach`-family release API is not provided.

#### Typed Option Capabilities

| Option Facade | Applies to |
|---|---|
| Common options (linger, HWM, timeout, and so on) | All |
| Router options (mandatory, handover, probe, connectRoutingId) | Router |
| Dealer options (probe) | Dealer |
| Stream options (notify) | Stream |
| Pub options (verbose, verboser, noDrop, manual, and so on) | Pub, XPub |
| Sub options (topicsCount) | Sub, XSub |
| RoutingId (set/get) | Dealer, Router, Stream |

  `disconnectRid`, `unbind`, and `close` are blocked.

## Per-Language Spec File Compliance Rule

Each per-language spec file (`doc/spec/bindings/{lang}/README.md`) must
follow the rules below. Apply this checklist when writing or reviewing a
spec file.

### Capability Matrix Consistency
- A per-language spec must not omit or add to the functionality in the
  Socket Capability Matrix above.
- Each socket-type class must provide every capability marked `Y` in the
  Socket Capability Matrix above, as a public surface that fits that
  language's convention.
- A capability marked `—` must not exist on that socket-type class in any
  language binding.
- Service-layer functionality the Socket Capability Matrix doesn't cover
  may be exposed as public API only when it's specified in a separate
  role matrix or policy section.
- Watch especially for these frequent violations:
  - No plain `send` (a send without routingId) on `RouterSocket`/
    `StreamSocket` — it must be `send(routingId, ...)`.
  - No `connect`, `disconnect`, or `disconnectRid` on `StreamSocket` —
    `STREAM` is a bind-only socket. Allowed only on Dealer, Router, Pub,
    Sub.
  - No `onSubscribe` callback on `XPubSocket` — only
    `receiveSubscriptionEvent` is allowed on XPub.
  - No `STREAM` raw direct callback `onReceive`, and no
    `detachStream`-family release API — the canonical surface is the
    `recv`/`setPacketHandler`/`close` combination.
  - Do not omit the shared socket TLS helper (`setTlsServer`,
    `setTlsClient`, or an equivalent name) — TLS configuration is
    shared transport functionality, so it must sit at the same location
    on every raw socket type.

### Required Argument For Routed Send
- `RouterSocket`'s and `StreamSocket`'s send must take routingId as a
  **required** argument.
- Making routingId an optional/default parameter is forbidden, because it
  would allow a plain send.

### Send/Publish Return Value
- On `.NET`/`Java`/`Node`/`Python`/`C++`, a blocking `send`/`publish`/
  callback `request` submit always returns `true` on success.
- On a non-blocking submit in those languages, it returns `false` only
  on temporary backpressure.
- A submit failure that is not temporary backpressure must be delivered
  as an exception.
- Returning a status code (`int`, `number`, and so on) is forbidden.

### Per-Language Naming Consistency
- A naming convention must not be mixed within one binding.
  - Python: every public API uses `snake_case` (including properties).
  - Java: `camelCase` methods, `PascalCase` classes.
  - C#: `PascalCase` throughout.
  - Go: `PascalCase` exported identifiers.
  - Rust: `snake_case` methods, `PascalCase` types.
  - C++: `snake_case` methods. Keep type naming consistent within one
    binding. A `_t` suffix can be used for a handle/value wrapper type,
    or where a type name would otherwise collide with a method name, but
    it is not forced onto every enum/class. Do not add a separate alias
    to the same type just to match a suffix rule.
  - Node/TypeScript: `camelCase` methods, `PascalCase` classes.

### Full C API Coverage
- Each per-language spec file must describe a binding interface that
  covers every `ZLINK_EXPORT` function in `core/include/zlink.h` and the
  public headers under `core/include/zlink/**`, without omission.
- The mapping does not have to be 1:1 (for example, a group of option
  functions can consolidate into a single typed facade).
- But no C API capability may be missing from a binding spec.
- When a new C API is added to a public header, every per-language spec
  file must be updated together.
## Service Layer Policy
- This section defines the public API policy for the service layer
  (Spot, Actor) that sits on top of the socket layer.
- The service layer follows the same POSD principles, naming policy,
  error policy, ownership policy, and testing policy as the socket
  layer.
- The service layer's baseline is the Spot/Actor C API in
  `core/include/zlink.h`.

### Spot / SpotNode Lifecycle (POSD Principles)

- **`SpotNode` is the lifecycle owner.** `Spot` is a pub/sub facade on
  top of it, valid only while `SpotNode` is alive.
- `Spot` is not built with an independent constructor. **It is created
  through a factory method such as `SpotNode.createSpot(...)`**. The
  name follows language idiom (`spot_node.new_spot`,
  `spotNode.createSpot`, and so on).
- The "get if it exists, otherwise create" flow keyed on an explicit
  Spot routing id is exposed through a `SpotNode.getOrCreateSpot(...)`
  family method that directly maps to
  `zlink_spot_node_spot_get_or_new(...)`. A binding must not emulate
  this meaning by combining lookup and create.
- `Spot`'s life is bound to its parent `SpotNode`.
  - `spot.close()` — ends only the Spot; the node stays alive
  - `spotNode.close()` — cleans up the node and every live Spot under it
    together (cascading close)
- This removes the need for a user to manually combine the close order
  of `Spot` and `SpotNode`. The binding pre-processes child spots inside
  `SpotNode.close()` before tearing down the node.
- The C API's raw `zlink_spot_new(...)` + `zlink_spot_node_new(...)`
  combination is not exposed as a binding public constructor as-is. It
  must be wrapped in a `SpotNode`-centered factory pattern.

### Service Layer Introspection Surface Tiers

The service layer's introspection/snapshot/entry types are **split into
two tiers by usage frequency**. A binding spec reflects this split.

- **Primary (core)**: a snapshot/query surface an ordinary user uses
  frequently. Described in `bindings/<lang>/README.md`'s upper section.
  - `SpotNodeStatus` (spot node status)

- **Advanced/Diagnostic**: for special purposes such as debugging or
  operational monitoring. Described in a separate "Advanced" or
  "Diagnostic" subsection in the spec.
  - `SpotNodePeerEntry`, `SpotNodeSubjectEntry`
  - `SpotNodeSocketEntry`, `SpotNodeSpotEntry`, `SpotNodeActorEntry`
  - Various filter types (`SpotNodePeerFilter`, `SpotNodeSubjectFilter`,
    `SpotNodeSocketFilter`)

The Primary types alone must be enough for a basic usage scenario. The
"register / discover / connect a service" flow must complete without
learning the Advanced types.

### Public Exposure Of `zlink_errno()`

- A binding **does not expose the raw `zlink_errno()`/`zlinkErrno()`
  function publicly**. Error detail is accessed **only ever through the
  error type's `internalErrno`/`internal_errno` field**.
- Do not create a dual path where a user investigating an error
  "sometimes uses `ZlinkException.getCode()` and sometimes uses
  `Zlink.errno()`" — unify it to one entry point.
- It's allowed for a binding's internal implementation to call
  `zlink_errno()` to fill in the exception object (for internal
  interpretation). The ban applies only to the public surface.
- A message-lookup utility such as `Zlink.strerror(errno)` can remain as
  a convenience, but the raw `errno()` accessor should be private or
  removed.

### Service Layer Architecture
- The service layer's current public axes are `SpotNode`, `Spot`,
  `Actor`, `StreamSocket`'s Actor binding surface, and the SPOT route
  bridge/publisher surface. The public Discovery/Registry handle was
  removed from the core contract in core 8.4.3, so it must not be
  revived as a new binding surface.

```
SpotNode
  |-- bind
  |-- raw mesh: connectPeer, disconnectPeer
  |   createPublisher
  |-- actor: create, lookup, remote create, join, leave
  |-- introspection: status, peers, peers(filter),
  |   subjects, spots, actors
  `-- TLS: setTlsServer, setTlsClient

Spot
  |-- publish, subscribe
  |-- sendToChannel, requestToChannel
  |-- sendToSpot, requestToSpot, requestToRouter
  |-- replyToSpot, replyToRouter
  |-- actor join: recvActorJoin, replyActorJoin, actors
  |-- actor lifecycle: recvActorLifecycle
  |-- setSubscription, unsetSubscription
  |-- setDispatchHandler, setSendReadyHandler
  `-- close facade only

Actor
  |-- ref: nodeRid, actorId, generation
  |-- receive: recvPart
  |-- bound session: send, close
  `-- close lifecycle handle

StreamSocket
  |-- bindActor, unbindActor
  `-- sendBoundActor

  |-- connect
  |-- snapshot
  `-- close
```

### Actor Dispatch Policy

Actor dispatch is a formal service-layer contract that currently exists
in core's public header. A binding does not hide Actor as an internal
SPOT detail — it organizes it as a separate public capability spanning
`SpotNode`, `Spot`, `Actor`, and `StreamSocket`.

If the language provides a unit for splitting public surface — a header,
module, package, namespace — Actor must have its own independent
entrypoint. This entrypoint must not be a thin forwarding file that
merely re-includes/imports/exports the whole SPOT header or module. The
Actor entrypoint must substantively own the public types and function
declarations that make up the Actor contract — the Actor value object,
the Actor lifecycle handle, the Actor recv/join helper. A structure where
the SPOT entrypoint reuses the Actor entrypoint is allowed, but a
structure where the Actor entrypoint exists only by leaning on the whole
SPOT implementation is non-compliant.

The baseline core public types and functions are as follows.

- Types: `zlink_actor_ref_t`, `zlink_actor_route_t`,
  `zlink_actor_recv_info_t`, `zlink_actor_join_info_t`,
  `zlink_actor_join_result_t`, `zlink_actor_join_entry_spot_result_t`,
  `zlink_actor_lookup_result_t`, `zlink_spot_actor_lifecycle_info_t`,
  `zlink_actor_join_spot_handler_fn`,
  `zlink_actor_join_entry_spot_handler_fn`,
  `zlink_actor_lookup_handler_fn`, `zlink_spot_node_spot_entry_t`,
  `zlink_spot_node_actor_entry_t`
- `SpotNode` axis: `zlink_spot_node_actor_new`,
  `zlink_spot_node_actor_lookup`, `zlink_remote_actor_get_ref` (async
  lookup), `zlink_spot_node_actor_destroy` (async submit),
  `zlink_spot_node_actor_join_spot` (async submit + a dedicated
  completion typedef), `zlink_spot_node_actor_join_entry_spot` (async
  submit + a dedicated completion typedef),
  `zlink_spot_node_actor_leave_spot` (async submit),
  `zlink_spot_node_actor_recv_part`,
  `zlink_spot_node_actor_send_bound_session_msg`,
  `zlink_spot_node_actor_reply_no_bind`,
  `zlink_spot_node_actor_close_bound_session`
- `Spot` axis: `zlink_spot_actor_join_recv`, `zlink_spot_actor_join_reply`,
  `zlink_spot_recv_actor_lifecycle`, `zlink_spot_actors`
- `StreamSocket` axis: `zlink_stream_bind_actor` (async submit),
  `zlink_stream_unbind_actor` (async submit),
  `zlink_stream_send_bound_actor_part`, `zlink_stream_bound_actors`
- Snapshot axis: `zlink_spot_node_spots`, `zlink_spot_node_actors`,
  `zlink_spot_actors`

The binding surface follows this split of responsibility.

| Public owner | Actor role |
|---|---|
| `SpotNode` | Local Actor create/lookup, async remote Actor lookup, async destroy, async join/leave, node-level Actor snapshot |
| `Actor` | Holds the Actor ref, Actor recv, bound STREAM session message send, bound session close |
| `Spot` | Actor join request recv/reply, Actor lifecycle event receive, a snapshot of Actors currently joined to this Spot |
| `StreamSocket` / session facade | Async STREAM session Actor bind/unbind, send targeted at a bound Actor, session-attach list lookup |

A binding must provide the following domain objects as a public
contract. The name can be converted to fit language convention, but the
field meaning does not change.

| Object | Required meaning |
|---|---|
| `ActorRef` | `node_rid`, `actor_id`, `generation` |
| `ActorRoute` | The routed target Actor, current Spot routing id, current Spot kind |
| `ActorRecvInfo` | The receiving Actor, source node/session routing id, flags |
| `ActorReceived` | `ActorRecvInfo` plus payload parts. The name can change per language convention, but the part-by-part loop and `has_more` are not exposed as public fields. In a language that owns the payload parts, expose it as a disposable envelope, not a cloneable record/value |
| `ActorJoinInfo` + join message | The `source_actor`, `target_actor`, `source_node_rid`, `source_spot_rid`, `target_node_rid`, `target_spot_rid`, `join_epoch`, `flags`, and join message needed to judge and respond to a join request. Can be grouped into an `ActorJoinRequest` wrapper or a tuple/pair per language convention. A wrapper that owns the join message must be disposable. The native reply context is kept only inside the binding and is not exposed as a public field |
| `ActorJoinResult` | Delivered on join completion. `result`, the final `actor` ref (the target node's ref for a remote join), `joined_spot_rid`, `join_epoch`, `flags` |
| `ActorJoinEntrySpotResult` | Delivered on Entry Spot join completion. `result`, the final `actor` ref, `target_node_rid`, `join_epoch`, `flags`. No join message or reply payload |
| `ActorLookupResult` | Delivered on remote Actor lookup completion. `result`, the checked `actor` ref, `flags` |
| `SpotActorLifecycleEvent` | The result of draining a Spot lifecycle readable event. `kind`, `info`. In a language where it also owns request parts, expose it as a disposable envelope, not a cloneable record/value |
| `SpotActorLifecycleInfo` | Included in a Spot lifecycle event. `previous_actor`, `current_actor`, `previous_spot_rid`, `current_spot_rid`, `join_epoch`, `flags` |
| `SpotNodeSpotEntry` | Spot routing id, Entry/User Spot kind, whether a dispatch handler is set, joined/pending Actor count, route sync state, change timestamp |
| `SpotNodeActorEntry` | Actor ref, current Spot routing id, current Spot kind, route sync state, pending message count, change timestamp |

The detailed rules are as follows.

- An Actor id is a non-empty UTF-8 string up to 255 bytes. A NUL
  character is not allowed.
- `generation == 0` is an unchecked remote ref, and is not treated as an
  invalid value.
- A local Actor is created by `SpotNode`, and its lifecycle handle is
  exposed as the per-language `Actor` type. An Actor can join only one
  Spot at a time.
- `leave` is an async submit API. It does not drain unread Actor
  messages. It always returns to the Entry Spot of the same node —
  if `leave` succeeds from a user Spot, a source-left event and an
  Entry-Spot-joined lifecycle event fire, and the active route is
  updated to the Entry Spot location.
- Entry Spot join is an async submit API. The target argument is the
  SpotNode rid, not an Entry Spot rid. Because a SpotNode has only one
  Entry Spot, the public API does not require a separate Entry Spot rid.
  An Entry Spot join does not send a join message and does not go
  through the application join queue. The completion handler returns
  only success/failure and the final Actor ref.
- An Actor that must start on a remote node is created by the
  application directly on that SpotNode with `actor_new`. A checked ref
  for a remote Actor is obtained through the async
  `remote_actor_get_ref` lookup. Remote create-or-get and an admission
  handler are not on the public surface.
- A Spot join request carries a message. A join reply must also return a
  message to the caller together with the accept/reject result. Join
  completion delivers the final Actor ref and joined Spot rid to the
  caller as an `ActorJoinResult` value.
- The request-reply surface exposes only the payload part the core reply
  function supports. Because the core reply function has no send-flag
  argument, a binding does not add a no-op flag-setting step to the
  reply builder.
- `ActorJoinInfo` exposing this does not mean it must expose every field
  of native `zlink_actor_join_info_t` as a public field. A per-language
  binding keeps the native request context needed for the reply as
  opaque internal state. The public value object exposes
  `source_actor`, `target_actor`, the source/target node and Spot
  routing id, `join_epoch`, `flags`, and the message — what a user needs
  to judge and respond.
- One STREAM session can bind multiple Actors. Bind/unbind is keyed on
  the session routing id and either the actor id or the Actor ref.
- When a language can naturally provide a session facade, it's better to
  expose STREAM Actor bind/unbind and send targeted at a bound Actor as
  operations on the session facade, rather than as socket-wide
  functions. This avoids repeatedly passing the session routing id.
- A public API that sends from STREAM to an Actor uses the bound session
  and actor id as its selector.
- Actor location is updated through the Actor creation, Spot join/leave,
  and Actor destroy flows. STREAM session bind/unbind does not change
  Actor location.
- There is no per-Actor queue-limit option. A binding must not make this
  a public option.
- A removed Actor ref function, a stream actor lookup/send helper, or a
  session-actor-key design name is not kept in the public surface or
  documentation.

An Actor dispatch event uses the same readiness model as the SPOT
dispatch event handler.

- `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE` is a notification that an
  Actor part can be read. One callback does not mean one part.
- `ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR`'s subject is a native Actor ref
  valid only during the callback. A binding's public API does not
  expose a raw pointer.
- A language that hands the callback off to a different execution
  context must non-blockingly pre-drain the Actor part at callback-entry
  time, so the public dispatch info can return that part.
- `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE` is the readiness signal
  for a Spot's Actor join request plane. A binding must let it be
  drained through `Spot.recvActorJoin` or an equivalent public surface
  until each language's no-data representation appears.

### SpotNode Capability Matrix

| Capability | SpotNode |
|---|---|
| `bind` | Y |
| `connectPeer` | Raw mesh only |
| `disconnectPeer` | Raw mesh only |
| `disconnectPeerRid` | Raw mesh only |
| `createSpot` | Y |
| `entrySpot` | Y |
| `spotLookup` | Y |
| `setTlsServer` | Y |
| `setTlsClient` | Y |
| `status` | Y |
| `peers` | Y |
| `peers(filter)` | Y |
| `subjects` | Y |
| `internalSockets` | Diagnostic |
| `spots` | Y |
| `actors` | Y |
| `close` | Y |

- SpotNode does not directly expose the data-plane API (`send`/`recv`/
  `publish`/`subscribe`).
- The data plane is accessed only through the `Spot` facade.
- `connectPeer`/`disconnectPeer` are control paths exclusive to raw peer
  topology.
- `createSpot` is a public factory placed on top of `zlink_spot_new()`.
  `entrySpot` wraps `zlink_spot_node_entry_spot()` in a per-language
  typed `Spot` factory. `spotLookup` wraps
  `zlink_spot_node_spot_lookup()` as a per-language typed `Spot` lookup
  surface. Treated as centered on `createPublisher`.

### Actor Capability Matrix

Actor dispatch is an independent service-layer capability spanning
`SpotNode`, `Actor`, `Spot`, and `StreamSocket`. Each binding must expose
the roles below as a public surface that fits its own language
convention.

| Capability | Public owner | Core substrate |
|---|---|---|
| local Actor create | `SpotNode` | `zlink_spot_node_actor_new` |
| local Actor lookup | `SpotNode` | `zlink_spot_node_actor_lookup` |
| unchecked remote Actor ref | `SpotNode` | `zlink_remote_actor_get_ref` |
| Actor destroy by ref | `SpotNode` | `zlink_spot_node_actor_destroy` |
| owned Actor close/destroy | `Actor` | `zlink_spot_node_actor_destroy` |
| Spot Actor lifecycle receive | `Spot` | `zlink_spot_recv_actor_lifecycle` |
| Actor join by ref | `SpotNode` | `zlink_spot_node_actor_join_spot` |
| Actor Entry Spot join by ref | `SpotNode` | `zlink_spot_node_actor_join_entry_spot` |
| owned Actor join | `Actor` | `zlink_spot_node_actor_join_spot` |
| owned Actor Entry Spot join | `Actor` | `zlink_spot_node_actor_join_entry_spot` |
| Actor leave by ref | `SpotNode` | `zlink_spot_node_actor_leave_spot` |
| owned Actor leave | `Actor` | `zlink_spot_node_actor_leave_spot` |
| Actor recv | `Actor` | `zlink_spot_node_actor_recv_part` |
| no-bind request reply | `SpotNode` | `zlink_spot_node_actor_reply_no_bind` |
| bound session send | `Actor` | `zlink_spot_node_actor_send_bound_session_msg` |
| bound session close | `Actor` | `zlink_spot_node_actor_close_bound_session` |
| join request recv | `Spot` | `zlink_spot_actor_join_recv` |
| join request reply | `Spot` | `zlink_spot_actor_join_reply` |
| STREAM bind Actor | `StreamSocket` / session facade | `zlink_stream_bind_actor` |
| STREAM unbind Actor | `StreamSocket` / session facade | `zlink_stream_unbind_actor` |
| STREAM send bound Actor | `StreamSocket` / session facade | `zlink_stream_send_bound_actor_part` |
| STREAM bound Actor snapshot | `StreamSocket` / session facade | `zlink_stream_bound_actors` |
| node Spot snapshot | `SpotNode` | `zlink_spot_node_spots` |
| node Actor snapshot | `SpotNode` | `zlink_spot_node_actors` |
| Spot joined Actor snapshot | `Spot` | `zlink_spot_actors` |

### Spot Capability Matrix

| Capability | Spot |
|---|---|
| `publish(topic, ...)` | Y |
| `subscribe` | Y |
| `receiveSubscriptionEvent` | Y |
| `setSubscription` / `unsetSubscription` | Y |
| `sendToChannel` / `requestToChannel` | Y |
| `sendToSpot` | Routed ordinary send (spot → spot) |
| `requestToSpot` | Routed request initiation (spot → spot) |
| `requestToRouter` | Routed request initiation (spot → router) |
| `replyToSpot` | Routed reply surface (spot → spot) |
| `replyToRouter` | Routed reply surface (spot → router) |
| `setDispatchHandler` | Y |
| `setSendReadyHandler` | Y |
| `recvActorLifecycle` | Y |
| `close` | Y |

- Spot is not a socket type — it's a channel-aware facade layered on top
  of SpotNode.
- Spot routed receive can be exposed as `recv_routed` or an equivalent
  typed recv surface.
- Spot has no `bind`/`connect` (SpotNode owns that).
- Spot's `close` releases only the facade — SpotNode stays alive.

### Removed Discovery/Registry Capability

The public Discovery and Registry C API was removed from the core
contract in core 8.4.3. A binding must not expose a Discovery/Registry
factory, resolver method, sync option, registry query client, or
compatibility alias as current API.

### Service Observability Policy
- Public service-layer observation uses a snapshot/query surface instead
  of a separate monitor handle.
- SPOT (SpotNode, Spot) observation uses the `status`, `peers`,
  `peers(filter)`, `subjects`, `spots`, and `actors` APIs. A binding that
  needs internal socket diagnostics keeps `internalSockets` as a
  separate diagnostic surface.
- When a state transition needs to be observed, compare successive
  snapshot/query results.
- The SocketMonitor callback release policy stays the same as before.
  - When a callback registration API exists, release it only through
    `close()`

### Service Layer Domain Objects
- The service layer must also use domain objects.
- The minimum core domain objects:
  - `MonitorStatus`: a monitor status snapshot
  - `SpotNodeStatus`: SpotNode status (state, peer count, and so on)
- Advanced/Diagnostic domain objects:
  - `SpotNodePeerEntry`: peer information
  - `SpotNodeSubjectEntry`: subject information
  - `SpotNodeSocketEntry`: internal socket diagnostic information. Uses
    the shared `SocketType` enum for the socket kind — does not create a
    separate SpotNode-only socket-type enum that repeats the same
    values.
  - `SpotNodeSpotEntry`: node-owned Spot information
  - `SpotNodeActorEntry`: node-owned Actor route information
- Filter objects:
  - `SpotNodePeerFilter`: a peer-lookup filter
  - `SpotNodeSubjectFilter`: a subject-lookup filter
  - `SpotNodeSocketFilter`: an internal socket diagnostic filter
- Enum/value objects:
  - `SocketType`: the socket kind shared between an ordinary socket and
    SpotNode's internal socket diagnostics
  - `SpotRole`: `PUB`, `SUB`
  - `SubjectKind`: `NONE`, `TOPIC`, `PATTERN`
  - `SpotNodeState`: `IDLE`, `CONNECTING`, `PARTIAL_READY`, `READY`,
    `ERROR`
  - `MonitorSourceKind`: `SOCKET`, `SPOT_PUB`, `SPOT_SUB`
  - `SpotPeerSource`: `MANUAL`, `DISCOVERY`, `MIXED`
  - `SpotPeerState`: `CONFIGURED`, `CONNECTING`, `CONNECTED`
- `MonitorStatus.isReady()` or an equivalent convenience accessor
  interprets ready meaning only for a raw socket monitor source. For a
  `SPOT_PUB`/`SPOT_SUB` source, the ready bit must not be
  reinterpreted as extended SPOT readiness.

### Service Layer Naming Policy
- The service layer also follows the Naming Policy.
- The allowed variation is the same three variations as the Naming
  Policy — casing variation, a minimal suffix for a language without
  overloads, and per-language property/getter convention only.
- Word substitution, omission, or replacement is forbidden.
- The detailed rules are the same as the Naming Policy body.

#### Service Layer Canonical Name Table

| Component | Canonical Name | Description |
|---|---|---|
| SpotNode | `bind` | Binds an endpoint |
| SpotNode | `connectPeer` | Connects a raw peer |
| SpotNode | `disconnectPeer` | Disconnects a raw peer |
| SpotNode | `createRouteBridge` | Registers a caller/channel-runtime-owned socket with the SPOT route bridge |
| SpotNode | `createPublisher` | Creates a publisher handle used for SpotNode's topic-publish ingress |
| SpotNode | `setTlsServer` | Configures TLS server |
| SpotNode | `setTlsClient` | Configures TLS client |
| SpotNode | `status` | A node status snapshot |
| SpotNode | `peers` | A peer-list snapshot |
| SpotNode | `peers(filter)` | A filtered peer lookup |
| SpotNode | `subjects` | A subject-list snapshot |
| SpotNode | `internalSockets` | An internal socket diagnostic snapshot |
| SpotNode | `spots` | A node-owned Spot snapshot |
| SpotNode | `actors` | A node-owned Actor snapshot |
| SpotNode | `close` | Terminates the node |
| Spot | `publish(topic, ...)` | Publishes a Spot topic |
| Spot | `subscribe` | Receives a topic subscription |
| Spot | `receiveSubscriptionEvent` | Receives a topic subscription event |
| Spot | `setSubscription` / `unsetSubscription` | Manages a subscription filter |
| Spot | `sendToChannel` / `requestToChannel` | A channel-targeted routed send/request |
| Spot | `setDispatchHandler` | Registers the topic/routed/channel-reply/timer readable notification handler |
| Spot | `setSendReadyHandler` | Registers the send-ready callback handler |
| Spot | `recvActorLifecycle` | Receives an Actor join/leave lifecycle event |
| Spot | `close` | Terminates the facade |

### Service Layer Test Policy
- Because the service layer includes components not directly verified
  by a sample or perf, it must be tested for correct FFI mapping,
  lifecycle, and type conversion.
- The service layer is tested using the same categories as the Test
  Matrix.

#### Service Layer Surface Tests
- Confirm SpotNode role-matrix alignment
- Confirm Spot role-matrix alignment
- Confirm the service TLS helper exists
- Confirm the typed domain objects exist (SpotNodeStatus,
  SpotNodePeerEntry, SpotNodeSocketEntry, SpotNodeSpotEntry,
  SpotNodeActorEntry, and so on)
- Confirm the typed enums exist (SpotRole, SubjectKind, SpotNodeState,
  and so on)

#### Service Layer Contract Tests
- SpotNode: no leak across the create/bind/close lifecycle
- Spot: create/close lifecycle (SpotNode must stay alive)
- Confirm native resources are cleaned up on exception/error paths too

#### Service Layer Behavior Tests
- SpotNode bind → Spot publish → Spot subscribe path succeeds
- Spot subscribe → returns empty when there's no data (non-blocking)
- Confirm exception on Spot publish failure
- Confirm the Spot dispatch event callback fires
- Confirm the Spot setSendReadyHandler callback fires
- Confirm the Spot receiveSubscriptionEvent path
- Confirm SpotRouteBridge attach/send/request/handleReceived paths work
- Confirm the SpotNode publisher handle publish path works

#### Service Layer Introspection Tests
- SpotNode status → verify SpotNodeStatus fields (state, peerCount,
  subjectCount, and so on)
- SpotNode peers → verify the SpotNodePeerEntry list
- SpotNode peers(filter) → verify the filtered result
- SpotNode subjects → verify the SpotNodeSubjectEntry list

#### Service Layer Test Scope

| Test Category | SpotNode+Spot | Actor | Stream Actor Binding |
|---|---|---|---|
| Surface | Required | Required | Required |
| Contract | Required | Required | Required |
| Behavior | Required | Required | Required |
| Introspection | Required | Required | Required |

- A binding without a service/spot family can exclude this test.
- Here, "monitor" refers to a socket monitor.

### Service Layer Sample Policy
- Service-family samples defined in the Canonical Sample Set:
  - `spot_recv_sample`: Spot channel-aware subscribe/routed recv
  - `spot_callback_sample`: Spot dispatch event callback
  - `monitor_recv_sample`: monitor event receive (including socket
    monitor)
- A binding without a service/spot family can exclude the `spot_*`
  samples.

### Per-Binding Service Layer Scope
- Not every binding has to implement the entire service layer.
- The minimum requirement:

| Component | Required level |
|---|---|
| SpotNode + Spot | Required if that binding has spot support |

### Callback API Policy
- A callback registration API is exposed according to each socket
  type's role.
- The Callback Capabilities table above is the baseline.
- Canonical handler registration names:
  - `setDispatchHandler`: registers the SPOT unified readable
    notification callback
  - `setSendReadyHandler`: registers the send-ready status callback
- SPOT routed receive and Actor lifecycle do not expose a direct
  callback registration API. `setDispatchHandler` announces a readable
  event, and the user explicitly drains the queue with `recvRouted` or
  `recvActorLifecycle`.
- `onReceive` may be used only as the internal name for the raw
  `STREAM` direct fragment callback. It is not used as a canonical
  public binding API name.
- Unregistering a callback by setting it to `null`/`None` is not
  allowed. A callback is unregistered only by closing the socket.

## 코어 API 추가 사항

이 섹션은 `core/include/zlink.h`에 추가된 core API를 정리한다.
각 바인딩은 이 API를 언어별 typed surface로 노출해야 한다.

### Request-Reply 정책

> 언어별 인터페이스 시그니처와 사용 예는
> `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 를 참조한다.

#### 설계 원칙

- request-reply 는 ZMP protocol envelope 로 처리한다.
  `zlink_msg_t` 에 request 표시를 붙이는 방식은 사용하지 않는다.
- dispatch, pending map, timeout, reply 매칭은 core C API 에서 처리한다.
  바인딩은 이 로직을 다시 구현하지 않는다.
- core 는 callback 기반 비동기 모델을 제공한다.
  바인딩은 [바인딩 비동기 실행 표면 정책](async-coroutine-policy.md)에 따라
  callback 위에 언어별 완료 객체 반환 표면을 얹을 수 있다. coroutine 연결은 framework가
  맡는다.
- `request()` 는 thread blocking API 가 아니다.
- request-reply 는 Router/Dealer 소켓과 SPOT 의 기능 확장이다.
  별도 추상 레이어가 아니라 기존 표면에 역할을 얹는다.

#### 공개 표면에 두지 않는 API

message-level request-reply marker API 와 per-message metadata API 는
public surface 의 일부가 아니다. 바인딩은 다음 함수나 상수를 public 으로
노출하지 않고, `Message` 객체 안에 request marker 상태를 두지 않는다.

- `zlink_msg_set_request`, `zlink_msg_set_reply`, `zlink_msg_get_request_info`
- `zlink_msg_set_metadata`, `zlink_msg_get_metadata`, `zlink_msg_clear_metadata`

#### 유효한 Request-Reply 조합

**Socket 경로:**

| 요청자 | 응답자 | 가능 | reply 경로 |
|--------|--------|------|-----------|
| Dealer | Router | Y | Router 가 Dealer 의 routing_id 로 회신 |
| Router | Router | Y | 서로 routing_id 로 회신 |
| Dealer | Dealer | **N** | 양쪽 다 routing_id 없음 |
| Router | Dealer | **N** | Dealer 가 특정 peer 에 회신 불가 |

**SPOT 경로:**

| 요청자 | 응답자 | 가능 | reply 경로 |
|--------|--------|------|-----------|
| Spot | Spot | Y | 상대 주소 + request_seq 로 회신 |
| Spot | Router | Y | Spot 이 Router 에 request, Router 가 Spot 에 reply |
| Router | Spot | Y | Router 가 Spot 에 request, Spot 이 Router 에 reply |

`DealerSocket.request()` 연결 제약:
- 연결 대상은 전부 Router 여야 한다.
  Dealer 에 Router 와 Dealer 가 섞이면 request 가 실패할 수 있다.
- 바인딩은 이 제약을 런타임에 검증하지 않는다. 사용자 책임이며 API 문서에 명시한다.

#### C API 표면

**공통 타입:**

```c
typedef void (*zlink_reply_handler_fn)(
    zlink_request_result_t result_,
    zlink_msg_t *parts,
    size_t part_count,
    void *userdata);

```

callback 으로 전달된 `parts` 는 borrowed view 다.
callback 반환 시점까지만 유효하다. 밖에서 유지하려면 복사한다.

**Socket API:**

```c
zlink_submit_result_t zlink_dealer_request_part(void *dealer,
    zlink_msg_t *part, zlink_send_flags_t flags,
    zlink_part_flag_t part_flag, uint32_t timeout_ms,
    zlink_reply_handler_fn handler, void *userdata);

zlink_submit_result_t zlink_dealer_reply_part(void *dealer,
    uint64_t request_seq, zlink_msg_t *part,
    zlink_part_flag_t part_flag);

zlink_submit_result_t zlink_router_request_part(void *router,
    const zlink_routing_id_t *peer_rid, zlink_msg_t *part,
    zlink_send_flags_t flags, zlink_part_flag_t part_flag,
    uint32_t timeout_ms, zlink_reply_handler_fn handler,
    void *userdata);

zlink_submit_result_t zlink_router_reply_part(void *router,
    const zlink_routing_id_t *peer_rid, uint64_t request_seq,
    zlink_msg_t *part, zlink_part_flag_t part_flag);

zlink_recv_result_t zlink_router_recv_part(void *router,
    const zlink_routing_id_t **source_node_rid_out,
    const zlink_routing_id_t **source_spot_rid_out,
    uint64_t *request_seq_out, zlink_msg_t *part_out,
    zlink_part_flag_t *has_more_out, zlink_recv_flags_t flags);
```

**SPOT API:**

```c
zlink_submit_result_t zlink_spot_send_channel_part(void *spot, ...);
zlink_submit_result_t zlink_spot_request_channel_part(void *spot, ...);
zlink_submit_result_t zlink_spot_send_spot_part(void *spot, ...);
zlink_submit_result_t zlink_spot_request_spot_part(void *spot, ...);
zlink_submit_result_t zlink_spot_request_router_part(void *spot, ...);
zlink_submit_result_t zlink_spot_reply_spot_part(void *spot, ...);
zlink_submit_result_t zlink_spot_reply_router_part(void *spot, ...);
zlink_submit_result_t zlink_router_request_spot_part(void *router, ...);
zlink_submit_result_t zlink_router_reply_spot_part(void *router, ...);
zlink_submit_result_t zlink_router_send_spot_part(void *router, ...);
zlink_submit_result_t zlink_spot_publish_part(void *spot, ...);
zlink_recv_result_t zlink_spot_subscribe_part(void *spot, ...);
zlink_recv_result_t zlink_spot_recv_part(void *spot, ...);
zlink_handler_result_t zlink_spot_dispatch_event_handler(void *spot, ...);
```

전체 시그니처는 `core/include/zlink.h` 를 참조한다.

#### 수신 Dispatch 모델

core 가 request-reply dispatch 를 처리한다. 바인딩은 dispatch owner 를 구현하지 않는다.

- `request_seq = 0` 이면 ordinary message.
- `request_seq != 0` 이면 request-reply message.
- core 가 pending map 에서 `source_node_rid + request_seq` 로 매칭한다.
- 매칭 실패한 reply (stray/late reply) 는 drop 한다.
- ROUTER 는 generic `zlink_recv_part()` 대신 `zlink_router_recv_part()` typed surface 를
  사용한다. generic `zlink_recv_part()` 호출 시 `EOPNOTSUPP`.
- ROUTER 의 routed 수신 plane 은 **단일 표면**이다. 일반 ROUTER 트래픽과
  spot-origin routed 트래픽 모두 `zlink_router_recv_part()` 하나로 받는다.
  `source_spot_rid` 가 `NULL` 이면 일반 ROUTER 트래픽, 채워져 있으면
  spot-origin 트래픽이다.

#### Request API 변형

request 는 두 완료 방식을 가진다.

비동기 request와 callback completion request는 모두 `request`
entrypoint가 반환하는 `RequestOp` operation builder를 통해 노출된다. 완료 방식별
flags, timeout, 실패 전달 규칙은 [바인딩 비동기 실행 표면 정책](async-coroutine-policy.md)을
따른다.

C binding 은 `zlink_*_request_part(..., flags, part_flag, timeout, ...)`
substrate 형태를 유지한다. C ABI에는 wrapper builder 정책을 적용하지 않는다.

- 에러 처리는 Error Handling Policy 를 따른다.
  callback request 의 submit 실패도 언어 관용구를 그대로 적용한다:
  exception 언어 (C++/Java/.NET/Node/Python) 는 예외, return-based 언어
  (C/Go/Rust) 는 에러 반환.
- reply 결과는 callback 이 정확히 한 번 전달한다.
  `(RequestResult result, List<Message> parts)`

#### SPOT Request-Reply

SPOT 직접 전달 위에서도 같은 request-reply 프로토콜을 사용한다.
`SPOT routed envelope -> request-reply envelope -> payload` 순서로 싣는다.
SPOT reply 도 ctx 없이 상대 주소 + request_seq 로 보낸다.
같은 Spot 에서 여러 request 를 동시에 outstanding 상태로 둘 수 있다.
high-level request 완료는 첫 reply 1건으로 끝난다.

#### Timeout

- timeout 은 core 가 관리한다. 바인딩은 timeout 로직을 구현하지 않는다.
- 기본 timeout: `5000ms`. per-call > socket default > 구현 기본 `5000ms`.
- `timeout_ms = 0` 이면 socket default timeout 을 사용한다.
- timeout 은 send 대기 + reply 대기를 합산한 전체 경과 시간에 적용된다.
- timeout 시 core 가 pending map 에서 제거하고 callback 에 `ZLINK_REQUEST_TIMED_OUT` 전달.
- timeout 후 late reply 는 core 가 drop 한다.

#### Pending map

- `request_seq` 채번, pending 등록, reply 매칭, timeout 제거 모두 core 에서 한다.
- 바인딩은 pending map 을 별도로 유지하지 않는다.
- 바인딩이 유지하는 것은 callback → Future/Promise resolve 매핑뿐이다.

#### Wire format

- `request_seq` 는 부호 없는 64비트 정수 (8바이트, network byte order).
- 시작값 `1`. `0` 은 ordinary message 예약값.
- overflow 시 `1` 로 wrap. outstanding 충돌값은 건너뛴다.
- envelope 은 4개 control part: protocol id, version, message type, request_seq.
- SPOT routed 조합 시 8개 SPOT control part + 4개 request-reply control part + payload.
- 바인딩은 envelope 을 직접 파싱하지 않는다. core 가 처리한다.

#### 반환 타입

- `request()` 성공 시 **reply payload `List<Message>` 만** 반환한다
  (`Vec<Message>` / `IReadOnlyList<Message>` / `Message[]` /
  `tuple[Message, ...]` 등 언어별 리스트 타입).
- caller 는 이미 자기가 보낸 request 의 대상 routing_id 와 request_seq 를
  알고 있으므로, 그걸 wrap 한 `Received` 를 되돌려받을 필요가 없다.
- 별도 `Reply` 타입은 만들지 않는다.
- multipart reply 지원이 목적이므로 단일 `Message` 가 아닌 리스트 형태다.
  단일 part reply 는 `parts[0]` 으로 꺼낸다.
- request handler (서버 측) 는 `peer_rid`, `request_seq`, payload 를 함께
  전달한다. 별도 `Request` 타입이나 `onRequest` 전용 callback 은 만들지
  않는다. (server 측은 누가 어떤 request_seq 로 보냈는지 알아야 하므로
  차이가 있다.)

#### 소유권

- `request()` / `reply()` 호출 시 메시지 ownership 은 기존 send 계약을 따른다.
- request callback 으로 전달된 `parts` 는 borrowed view 다.
  callback 반환 후 무효. 바인딩은 이를 복사해 언어별 리스트 타입 또는
  `Vec<Message>` 로 전달한다.
- 소켓 close 시 core 가 pending map 의 모든 미완료 request 를 `ZLINK_REQUEST_TERMINATED` callback 으로 reject 한다.

#### Callback 계약

- callback 은 정확히 한 번 호출된다.
  성공이면 `result = OK` + reply parts, 실패면 `result != OK` +
  empty/null/Err 경로로 전달된다.
- core callback 시그니처: `void(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, void *userdata_)`
- 언어별 패턴 (per-function `RequestError` 계승):
  - C++: `std::function<void(request_result_t, std::vector<message_t>)>`
  - Java: `BiConsumer<RequestResult, List<Message>>`
  - .NET: `Action<RequestResult, IReadOnlyList<Message>>`
  - Node: `(result: RequestResult, parts: Message[]) => void`
  - Python: `callback(result: RequestResult, parts: list[Message])`
  - Go: `func(RequestResult, []*Message)` (실패 시 nil/empty 허용)
  - Rust: `FnOnce(Result<Vec<Message>, RequestError>)` (Rust 관용구;
    `RequestError::code` 가 `RequestResult` 에 대응)

### SPOT Messaging 정책

> 언어별 SPOT 인터페이스는 `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 를 참조한다.

SPOT public surface 는 두 이름 축을 분리한다. `sendToChannel(...)` 과
`requestToChannel(...)` 은 channel-aware 직접 메시징 경로이고,
`publish(topic, ...)` 는 `Spot` 자신이 속한 topic plane 에 발행한다.
직접 주소 지정 routed messaging 은 선택적으로 추가할 수 있는 보조 typed surface 다. request-reply 는
routed messaging 위에 얹어진다.

#### Pub/Sub 메시징

SPOT pub/sub 는 `Spot` handle 이 속한 channel 과 `topic` 기반 발행/구독 모델이다.
발행 호출자는 channel 이름을 별도 인자로 전달하지 않는다.

```c
/* publish */
zlink_submit_result_t zlink_spot_publish_part(void *spot,
    const char *topic_id, zlink_msg_t *part, zlink_send_flags_t flags,
    zlink_part_flag_t part_flag);

/* subscribe receive */
zlink_recv_result_t zlink_spot_subscribe_part(void *spot,
    const zlink_routing_id_t **source_rid_out,
    char *topic_id_buf, size_t topic_id_capacity,
    size_t *topic_id_len_out, zlink_msg_t *part_out,
    zlink_part_flag_t *has_more_out, zlink_recv_flags_t flags);

/* subscription filter */
zlink_config_result_t zlink_set_subscription(
    void *handle,
    const char *filter);
zlink_config_result_t zlink_unset_subscription(
    void *handle,
    const char *filter);
```

바인딩 규칙:
- C API 는 publish 를 위한 별도 no-wait 함수 이름을 따로 두지 않는다.
- non-blocking publish 는 `zlink_spot_publish_part(..., ZLINK_DONTWAIT, ...)` 를 호출하고
  errno 를 `zlink_submit_result_t` 로 분류한다. 바인딩은 별도 `tryPublish` 나
  `publishNoWait` 를 두지 않는다.
- `subscribe` 수신은 `topic + parts` 를 돌려주는 typed receive surface 로
  노출한다.
- topic filter 설정은 typed subscription API 로 노출한다.
- channel-aware send/request 와 topic publish 의 실패는 `SubmitError` 로 승격된다.
  - `NOT_FOUND`: channel-aware send/request 는 해당 `channel_name` 또는 attach 대상이 없음.
    topic publish 는 발행 가능한 topic plane 대상이 없음.
  - `NOT_CONNECTED`: attachment 는 있으나 active/send-ready 경로가 없음
  - `BACKPRESSURED`: 경로는 있으나 HWM 도달
  - `NOT_ADMITTED`: 대상 peer 가 drain 상태라 신규 submit 거부

#### Routed Direct Messaging

SPOT routed direct messaging 은 특정 Spot 또는 Router peer, routed reply 대상에
직접 메시지를 보낸다. Core substrate는 아래 part 기반 C 함수로 표현된다.
고수준 바인딩의 `Spot` facade와 `RouterSocket`의 router-to-spot helper 모두
이 기능을 `Operation Builder Policy`에 맞춘 operation builder 시작점으로
노출한다. raw socket의 일반 send/request/reply도 동일한 builder 패턴을 따른다.

```c
/* spot -> spot */
zlink_submit_result_t zlink_spot_send_spot_part(void *spot,
    const zlink_routing_id_t *dest_node_rid,
    const zlink_routing_id_t *dest_spot_rid,
    zlink_msg_t *part, zlink_send_flags_t flags,
    zlink_part_flag_t part_flag);

/* router -> spot */
zlink_submit_result_t zlink_router_send_spot_part(void *router,
    const zlink_routing_id_t *dest_node_rid,
    const zlink_routing_id_t *dest_spot_rid,
    zlink_msg_t *part, zlink_send_flags_t flags,
    zlink_part_flag_t part_flag);
```

바인딩 규칙:
- C ABI는 part 기반 함수형 계약을 유지한다.
- 고수준 바인딩의 `Spot` endpoint, `RouterSocket`의 router-to-spot helper,
  그리고 raw `DealerSocket`/`RouterSocket`/`PubSocket`/`StreamSocket` 등의
  일반 send/request/reply/publish 표면 모두 이 문서의
  `Operation Builder Policy`를 따른다.
- 목적지 주소·요청 시퀀스는 builder 시작점 인자로 받고, payload·flags·timeout·
  callback은 builder 단계로 표현한다.
- routed recv 는 아래 Event Dispatcher 의 handler/recv surface 를 사용한다.

#### SPOT Lifecycle / Bridge / Deprecated Attachment

```c
void *zlink_spot_new(void *node);          /* create SPOT facade */
zlink_close_result_t zlink_spot_destroy(void **spot_p);

void *zlink_spot_node_new(
    void *ctx,
    const zlink_spot_node_options_t *options);
zlink_close_result_t zlink_spot_node_destroy(void **node_p);
zlink_bind_result_t zlink_spot_node_bind(void *node, const char *endpoint);
zlink_connect_result_t zlink_spot_node_connect_peer(void *node,
    const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer(void *node,
    const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer_rid(void *node,
    const zlink_routing_id_t *peer_rid);

void *zlink_spot_route_bridge_new(
    void *ctx,
    void *spot_node,
    const zlink_spot_route_bridge_options_t *options);
int zlink_spot_route_bridge_attach_router_channel(
    void *bridge,
    const char *channel_name,
    void *router,
    const zlink_spot_route_bridge_endpoint_options_t *options);
int zlink_spot_route_bridge_send(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *target_node_rid,
    const zlink_routing_id_t *target_spot_rid,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_send_flags_t flags);
int zlink_spot_route_bridge_request(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *target_node_rid,
    const zlink_routing_id_t *target_spot_rid,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_reply_handler_fn reply_handler,
    void *userdata,
    zlink_send_flags_t flags,
    uint32_t timeout_ms);
int zlink_spot_route_bridge_handle_router_received(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *source_node_rid,
    uint64_t request_seq,
    zlink_msg_t *parts,
    size_t part_count,
    bool *handled_out);
int zlink_spot_route_bridge_drain(void *bridge);
int zlink_spot_route_bridge_close(void *bridge);

void *zlink_spot_node_publisher_new(void *node);
int zlink_spot_node_publisher_publish(
    void *publisher,
    const char *topic,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_send_flags_t flags);
int zlink_spot_node_publisher_close(void *publisher);
```

`options == NULL` 또는 `options->mode == 0`은 모든 SPOT 기능을 켠다. 바인딩은
각 언어의 기본 생성자에서 이 기본값을 사용하고, mode를 노출하는 경우
`PUBSUB`, `ROUTED`, `ALL` 값을 C 계약과 같은 의미로 매핑한다. 내부 socket
관찰 API는 `zlink_spot_node_internal_sockets()`을 기준으로 하며,
이미 생성된 socket만 반환한다.

SpotNode option facade는 core의 여섯 public option을 빠뜨리지 않아야 한다.

| Core option | Binding surface |
|-------------|-----------------|
| `ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE` | router admission HWM profile |
| `ZLINK_SPOT_NODE_OPT_ROUTER_HWM` | router admission HWM override |
| `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE` | pub/sub admission HWM profile |
| `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM` | pub/sub admission HWM override |
| `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN` | minimum dispatch callback workers |
| `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX` | maximum dispatch callback workers |

dispatch worker min/max는 `SpotNode` dispatch callback 실행 pool 설정이다.
data-plane thread나 transport I/O thread 수를 바꾸는 옵션으로 설명하면 안 된다.
값 검증은 core와 동일하게 `min >= 1`, `max >= min`이다. 바인딩은 각 언어의
typed option/property로 이 두 값을 노출하고, raw option bag을 canonical 경로로
되살리면 안 된다.

바인딩 규칙:
- `SpotNode` 와 `Spot` 은 별도 typed handle 로 노출한다.
- `Spot` 은 `SpotNode` 위에 올라가는 facade 다. `SpotNode` 해제 시 `Spot` 도 무효가 된다.
- Spot에서 다른 channel로 보내거나 `ROUTER` channel에서 Spot relay packet을 받을 때는
  `SpotRouteBridge`를 사용한다. bridge에 등록되는 `ROUTER` socket은 caller 또는
  channel runtime이 계속 소유한다.
- raw Core socket에는 logical channel metadata를 설정하거나 조회하는 API가
  없다. channel name은 `SpotRouteBridge`의 typed operation이 받는 논리적
  routing 값으로만 사용한다.
- bridge의 `handle_router_received()`는 channel runtime의 receive loop에서 호출한다.
  `handled == true`이면 payload 소유권은 bridge가 가져가며, caller는 같은 received
  object를 다시 처리하지 않는다.
- `SpotNodePublisher`는 외부 코드가 raw `PUB` socket을 `SpotNode`에 attach하지 않고
  SpotNode의 topic publish ingress로 publish하기 위한 handle이다.
- `Spot.publish(topic).message(...).submit()`은 `SpotNode` 자신의 topic publish
  ingress queue로 들어가는 channel-aware topic plane이다. 외부 channel 호출은
  `SpotRouteBridge`와 channel runtime 소유 socket 경로로 설명한다.
- `connect_peer` / `disconnect_peer` 는 raw peer topology 전용 control
  path 다. channel-aware public surface 의 중심 API 로 설명하면 안 된다.

### SPOT Event Dispatcher 정책

core 는 callback 기반 event dispatcher 모델을 제공한다.
하나의 I/O thread context 안에서 여러 이벤트 소스
(sub recv, routed recv, timer, send-ready) 를 동기화 없이 처리할 수 있다.

핵심 원리:
- handler callback 을 등록하면 core I/O thread 가 이벤트 발생 시 callback 을 호출한다.
- 모든 callback 은 같은 thread context 에서 실행되므로 lock 없이 상태를 공유할 수 있다.
- callback 안에서 recv, send, reply 를 호출해도 동기화 문제가 없다.
- timer 도 같은 context 에서 실행된다.

#### Callback 등록 API

```c
/* raw STREAM direct recv callback */
zlink_handler_result_t zlink_recv_handler(void *s,
    zlink_socket_msg_handler_fn handler, void *userdata);

/* raw STREAM packet callback */
zlink_handler_result_t zlink_stream_packet_handler(void *stream,
    zlink_stream_packet_handler_fn handler, void *userdata);

/* register writable notification callback */
zlink_handler_result_t zlink_send_ready_handler(void *s,
    zlink_send_ready_handler_fn handler, void *userdata);
```

규칙:
- core C attach 함수는 한 subject 당 활성 handler 하나만 허용한다.
  이미 native handler가 attach된 상태에서 다시 attach하면 `EBUSY`가 날 수 있다.
  public binding의 `set...Handler` 표면은 이 raw attach 함수를 직접 반복 노출하지
  않고, 현재 public handler를 저장하거나 교체하는 의미로 제공한다.
- `zlink_recv_handler()` 는 raw `STREAM` 에만 허용한다.
- `zlink_stream_packet_handler()` 도 raw `STREAM` 에만 허용하며,
  `recv` / raw callback / packet callback 세 모드는 서로 배타적이다.
- raw `PAIR`, `DEALER`, `ROUTER`, `SUB`, `XSUB` 는 direct receive callback
  install surface 를 두지 않는다. `PAIR`, `DEALER`, `ROUTER` 는 공개
  recv 메서드로만 수신하고, `SUB`, `XSUB` 는 topic subscribe 수신 표면으로만
  수신한다.
- callback 등록 후 같은 subject 에 대한 direct recv 와 해당 data-plane
  `ZLINK_POLLIN` 등록은 `EBUSY` 로 실패할 수 있다. 정확한 적용 범위는
  STREAM / SPOT 의 타입별 규칙을 따른다.
- public callback setter는 replace-only다. `NULL` 전달은 허용하지 않는다.

#### Spot Dispatch Event Handler

Spot 의 핵심 event dispatcher 는 `zlink_spot_dispatch_event_handler()` 다.
이 handler 를 등록하면 Spot 에 관련된 모든 이벤트가 하나의 callback 으로 올라온다.
같은 `spot` 에 대해서는 callback 이 순차적으로 전달되어야 한다. 구현은 같은
`spot` 의 dispatch callback 을 동시에 호출하거나 재진입 호출해서는 안 된다.
callback 안에서 event 종류를 확인하고 recv 를 호출하면서 Spot 메시징을
순차적으로 처리할 수 있어야 한다.

이 직렬화는 `spot` 단위다. 서로 다른 `spot` 사이에는 전역 직렬화를 요구하지
않는다. 구현은 다른 Spot 들을 병렬로 처리할 수 있어야 하며, 그 과정에서도
같은 `spot` 의 순차 처리 계약은 유지되어야 한다.

```c
typedef enum zlink_spot_dispatch_event_t {
    ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE = 1,
    ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE = 2,
    ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE = 3,
    ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE = 4,
    ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE = 5,
    ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE = 6
} zlink_spot_dispatch_event_t;

typedef enum zlink_spot_dispatch_subject_kind_t {
    ZLINK_SPOT_DISPATCH_SUBJECT_SPOT = 1,
    ZLINK_SPOT_DISPATCH_SUBJECT_TIMER = 2,
    ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER = 3,
    ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR = 4
} zlink_spot_dispatch_subject_kind_t;

typedef struct zlink_spot_dispatch_info_t {
    zlink_spot_dispatch_event_t event;
    zlink_spot_dispatch_subject_kind_t subject_kind;
    void *subject;
} zlink_spot_dispatch_info_t;

typedef void (*zlink_spot_dispatch_event_handler_fn)(
    void *spot, const zlink_spot_dispatch_info_t *info, void *userdata);

zlink_handler_result_t zlink_spot_dispatch_event_handler(void *spot,
    zlink_spot_dispatch_event_handler_fn handler, void *userdata);
```

사용 패턴:
- dispatch event handler 를 등록한다.
- callback 이 호출되면 `info->event`, `info->subject_kind`, `info->subject`를 확인한다.
- 같은 `spot` 의 활성 dispatch callback 안에서는 기본 recv surface 를 사용할 수 있다.
- `SUBSCRIBE_READABLE` 이면 `zlink_spot_subscribe_part()` 또는
  `zlink_spot_recv_subscription_event()` 로 pub/sub plane 을 drain 한다.
- `ROUTED_READABLE` 이면 `zlink_spot_recv_part()` 로 routed/request 메시지를 recv 한다.
- `TIMER_READABLE` 이면 `info->subject` timer handle에 대해 `zlink_timer_recv()` 로 timer fire 를 recv 한다.
- `CHANNEL_REPLY_READABLE` 은 readiness 신호일 뿐이며 별도 public drain API 는
  없다. reply 는 `zlink_spot_request_channel_part()` 호출 시 등록한
  `zlink_reply_handler_fn` 을 통해 core 가 자동으로 전달한다. `info->subject`
  dealer handle 은 deprecated dealer attach 경로에서만 의미가 있는 진단 정보다.
- `ACTOR_READABLE` 이면 `info->subject` 로 전달된 Actor subject를 기준으로
  `zlink_spot_node_actor_recv_part()` 를 drain 한다. public API는 raw subject
  pointer나 part loop를 노출하지 않고 `ActorReceived` 또는 동등한 aggregate
  값 객체를 돌려준다.
- `ACTOR_JOIN_READABLE` 이면 `zlink_spot_actor_join_recv()` 로 join request
  plane 을 drain 한다.
- dispatch event 는 readable 알림이다. callback 1회가 메시지 1개를 뜻하지는 않는다.
- callback 안에서는 해당 plane 을 더 이상 읽을 것이 없을 때까지 drain 할 수 있어야 한다.
- `zlink_spot_recv_part()` 의 첫 호출은 hidden activation, hidden queue open, hidden registration 을 수행하면 안 된다.
- 같은 `spot` 의 dispatch callback 은 직렬화되므로 Spot 메시징을 순차적으로 처리할 수 있다.
- 서로 다른 `spot` 은 병렬 처리될 수 있으므로 고성능 room 실행 모델을 구성할 수 있다.

#### Spot Timer API

Spot 소유 timer 는 `zlink_spot_timer_new(spot)` 로 생성하고, 이후 공통
`zlink_timer_*` 함수로 제어한다.

```c
void *zlink_spot_timer_new(void *spot);

/* use the common timer API after creation */
zlink_close_result_t zlink_timer_destroy(void **timer_p);
zlink_config_result_t zlink_timer_start(void *timer,
    uint64_t interval_ns, uint64_t repeat_count);
zlink_config_result_t zlink_timer_stop(void *timer);

typedef void (*zlink_timer_handler_fn)(
    void *timer, uint64_t fire_count, void *userdata);

zlink_handler_result_t zlink_timer_handler(void *timer,
    zlink_timer_handler_fn handler, void *userdata);
zlink_recv_result_t zlink_timer_recv(void *timer, uint64_t *fire_count_out);
```

규칙:
- timer 는 `zlink_spot_timer_new(spot)` 로 Spot 에 종속하여 생성한다.
- 생성 후에는 `zlink_timer_start`, `zlink_timer_stop`, `zlink_timer_recv`,
  `zlink_timer_handler`, `zlink_timer_destroy` 공통 API로 제어한다.
- `interval_ns` 는 나노초 단위다. `repeat_count = 0` 이면 무한 반복.
- timer fire 는 dispatch event handler 에 `TIMER_READABLE` 로 올라온다.
- timer handler callback 을 직접 등록하거나 `zlink_timer_recv()` 로 polling 할 수 있다.
- dispatch callback 안에서는 `zlink_timer_recv()` 로 pending fire 를 순차 처리할 수 있다.

바인딩 규칙:
- timer 는 typed wrapper 로 노출한다.
- `interval_ns` 는 언어별 Duration 타입으로 변환한다.
- timer 와 dispatch event 를 통합하여, 사용자는 callback 등록만으로
  sub recv + routed recv + timer 를 동기화 없이 처리할 수 있어야 한다.

#### Dispatch 모델 요약

```
zlink_spot_dispatch_event_handler callback
  (serialized per spot, non-reentrant)
  |-- SUBSCRIBE_READABLE -> zlink_spot_subscribe_part()
  |                         or zlink_spot_recv_subscription_event()
  |-- ROUTED_READABLE -> zlink_spot_recv_part()
  |-- TIMER_READABLE -> zlink_timer_recv()
  |-- CHANNEL_REPLY_READABLE -> readiness only; reply handler runs internally
  |-- ACTOR_READABLE -> zlink_spot_node_actor_recv_part()
  `-- ACTOR_JOIN_READABLE -> zlink_spot_actor_join_recv()
```

같은 `spot` 에 대해서는 이 callback 안에서 recv, send, reply 를 순차적으로
처리할 수 있어야 한다.
서로 다른 `spot` 은 필요하면 병렬로 실행될 수 있어야 한다.
callback 안에서는 event 로 알려진 plane 을 drain 할 수 있어야 한다.

#### Receive-model 요약

| 소켓 타입 | 수신 경로 |
|-----------|----------|
| `PAIR` / `DEALER` | runtime은 `zlink_recv_part()` 를 사용하고 public 표면은 aggregate recv |
| `SUB` / `XSUB` | runtime은 `zlink_subscribe_part()` 를 사용하고 public 표면은 aggregate topic recv |
| `ROUTER` | runtime은 `zlink_router_recv_part()` 를 사용하고 public 표면은 aggregate routed recv. request completion 은 `zlink_reply_handler_fn` 으로 유지 |
| `STREAM` | 아래 세 모드 중 하나 (상호 배타). raw recv / `zlink_recv_handler()` / `zlink_stream_packet_handler()` |
| `SPOT` | `zlink_spot_recv_part()` + `zlink_spot_subscribe_part()` + `zlink_spot_recv_subscription_event()` + `zlink_spot_recv_actor_lifecycle()` + `zlink_spot_dispatch_event_handler()`. direct routed callback은 노출하지 않는다 |

바인딩은 위 계약을 구현에 그대로 반영한다. public 소켓 클래스에는 aggregate
recv 표면만 노출하고, 금지된 callback install surface 는 base 클래스 어디에서도 우회 접근되지
않도록 한다.

#### Typed Receive Surface

SPOT 수신은 여러 typed surface 를 제공한다.
바인딩은 이 typed surface 위에 언어별 handler/callback 표면을 얹는다.

#### Spot 수신

```c
zlink_recv_result_t zlink_spot_recv_part(void *spot, ...);
zlink_recv_result_t zlink_spot_recv_actor_lifecycle(void *spot, ...);
```

- `request_seq = 0` 이면 ordinary routed message다.
- `request_seq != 0` 이면 request-reply message다.
- `source_rid + spot_rid` 는 발신자 주소이며 reply target 으로 사용한다.
- 바인딩 public API는 part helper 대신 aggregate `Received` 또는 언어별 동등 타입을 노출한다.
- Actor lifecycle은 dispatch event 뒤 `zlink_spot_recv_actor_lifecycle()`로 drain한다.

#### Router 수신 (routed 통합 recv 표면)

```c
zlink_recv_result_t zlink_router_recv_part(void *router,
    const zlink_routing_id_t **source_node_rid_out,
    const zlink_routing_id_t **source_spot_rid_out,
    uint64_t *request_seq_out,
    zlink_msg_t *part_out, zlink_part_flag_t *has_more_out,
    zlink_recv_flags_t flags);
```

- ROUTER 의 routed 수신은 단일 plane 이다. 일반 ROUTER 트래픽과
  spot-origin routed 트래픽을 하나의 recv 로 받는다.
- `source_spot_rid == NULL` 이면 일반 ROUTER 트래픽 (reply 는
  `zlink_router_reply_part` 사용). `source_spot_rid` 가 채워져 있으면
  spot-origin 트래픽 (reply 는 `zlink_router_reply_spot_part` 사용).
- `request_seq == 0` 이면 fire-and-forget. `request_seq != 0` 이면 request.
- 바인딩은 ROUTER data-plane callback install surface 를 별도로 노출하지 않는다.
  request completion callback 은 `request(...)` 경로에서만 유지한다.

#### Pub/Sub 수신

- raw `SUB`, `XSUB` 는 수신 전용 topic socket 이다.
- 바인딩은 `zlink_subscribe_part()` typed receive substrate 위에 언어별
  aggregate topic receive surface 를 노출한다.
- direct topic callback install surface 는 raw pub/sub family 에 두지 않는다.

#### SPOT Snapshot Query

```c
zlink_config_result_t zlink_spot_node_status(void *node,
    zlink_spot_node_status_t *out);
zlink_config_result_t zlink_spot_node_peers(void *node,
    zlink_spot_node_peer_entry_t *entries, size_t *count);
zlink_config_result_t zlink_spot_node_peers(void *node,
    const zlink_spot_node_peer_filter_t *filter,
    zlink_spot_node_peer_entry_t *entries, size_t *count);
zlink_config_result_t zlink_spot_node_subjects(void *node,
    const zlink_spot_node_subject_filter_t *filter,
    zlink_spot_node_subject_entry_t *entries, size_t *count);
zlink_config_result_t zlink_spot_node_internal_sockets(void *node,
    const zlink_spot_node_socket_filter_t *filter,
    zlink_spot_node_socket_entry_t *entries, size_t *count);
zlink_config_result_t zlink_spot_node_spots(void *node,
    zlink_spot_node_spot_entry_t *entries, size_t *count);
zlink_config_result_t zlink_spot_node_actors(void *node,
    zlink_spot_node_actor_entry_t *entries, size_t *count);
zlink_config_result_t zlink_spot_actors(void *spot,
    zlink_actor_ref_t *entries, size_t *count);
```

바인딩 규칙:
- snapshot 결과는 언어별 typed domain object 배열로 변환한다.
- filter query 는 typed filter builder 또는 struct 로 노출한다.
- 반환된 배열의 메모리는 바인딩이 적절히 해제해야 한다.

### SpotNode Node-Level 옵션

SpotNode의 node-level 옵션은 `zlink_set_spot_node_option()` 계열로 다룬다.

## 옵션 정책

### 공개 옵션 표면
- **public raw `setOption(key, value)` / `getOption(key)` bag 은 금지.**
- **public raw `setsockopt/getsockopt` bag 도 금지.**
- 공용 옵션은 언어에 맞는 typed surface (facade) 로만 노출한다.
- 특화 옵션도 언어에 맞는 역할 surface (facade) 로만 노출한다.
- raw enum key + 범용 setter/getter 를 돌리는 public 경로가 spec 에
  남아 있으면 정책 위반. (`set_option(ZLINK_OPT_*, value)` 같은 C 계약이
  바인딩 public API 로 올라오면 안 됨. 바인딩 내부에서 native 호출 경로는
  허용.)
- typed facade 가 이미 있으면 **raw 경로를 중복 노출하지 않는다** — 사용자가
  두 방식 중 고를 필요 없게 한다.
- 예:
  - Java/.NET: `CommonSocketOptions`, `RouterSocketOptions`
  - Go: typed method set, 역할 interface
  - Rust: typed builder, method set, newtype
  - Python/Node: property, namespace object, 역할 object, typed method set

#### Option Facade Canonical 타입 이름
- 각 바인딩은 아래 canonical facade 타입을 제공해야 한다.
- 타입 이름은 언어 케이싱 관례만 변형한다.

| Facade | 내용 | 적용 소켓 |
|---|---|---|
| `CommonSocketOptions` | linger, sendHighWaterMark, receiveHighWaterMark, sendTimeout, receiveTimeout, immediate, connectTimeout, ipv6, tcpNoDelay, tcpKeepAlive, heartbeatInterval/Ttl/Timeout, maxMessageSize, backlog, reconnectInterval/Max, submitRetryMode, submitRetryTimeout, submitRetryAttempts | 전체 |
| `RouterSocketOptions` | mandatory (bool), handover (bool), probe (bool), connectRoutingId (RoutingId), requestTimeout (Duration), peerWeight (int, read/write) | Router |
| `DealerSocketOptions` | probe (bool), requestTimeout (Duration), peerWeight (int, read/write) | Dealer |
| `StreamSocketOptions` | notify (bool) | Stream |
| `PubSocketOptions` | verbose (bool), verboser (bool), noDrop (bool), manual (bool) | Pub, XPub |
| `SubSocketOptions` | topicsCount (int, read-only) | Sub, XSub |

- 각 facade의 option 항목은 `core/include/zlink.h`의 해당 option enum 값을
  기준으로 한다.
- facade 내 option 값 타입은 Option Value Types 정책을 따른다.
- submit retry option은 raw socket facade에서 기본값을 off/0ms/0회로 노출한다.
  managed SPOT/service 내부 profile은 `LOCAL_FAILURE`/100ms/2회를 사용할 수 있지만,
  raw socket option 기본값을 바꾸지 않는다. `DONTWAIT` 호출, backpressure, admission
  거절, request submit 성공 뒤의 reply timeout은 submit retry 대상이 아니다.

### 옵션 값 타입
- option 값은 가능한 한 의미 기반 타입으로 노출한다.
- 정책:
  - `0/1` 옵션: `boolean`
  - 유한 상태 집합: `enum`
  - 시간 의미: `Duration` 또는 언어 표준 시간 타입
  - binary identifier: `RoutingId` 같은 value object
  - 진짜 수치 설정: `int`/`long`
  - 문자열/바이트: `String`/`byte[]`
- option 이름만 enum이고 값은 raw `int`인 형태는 충분하지 않다.

## 성능 정책
- 성능은 별도 최적화 항목이 아니라 public API 설계의 일부다.
- canonical hot path는 숨은 비용이 가장 적은 경로여야 한다.
- hot path에서는 다음을 기본적으로 금지한다.
  - 숨은 payload 복사
  - 숨은 배열/리스트 재할당
  - 불필요한 UTF-8 인코딩/디코딩
  - 바인딩 레이어의 중복 포장
  - 결과를 만들기 위한 불필요한 boxing/unboxing
- 편의 API는 기본 경로보다 비용이 더 크면 문서화해야 한다.
- callback path와 direct receive path는 payload shape뿐 아니라 비용 모델도
  과도하게 벌어지면 안 된다.
- zero-copy, borrowed, owned 경로가 다르면 ownership과 함께 비용 모델도
  문서화해야 한다.
- 성능 검증 강도는 언어와 런타임 특성에 따라 달라질 수 있다.
- 다만 모든 바인딩은 hot path에서 불필요한 복사, 할당, 변환을 줄이는 방향을
  기본 정책으로 삼아야 한다.

### 고성능 버퍼 생태계 정책 (Recommended)
- canonical public contract 는 계속 `Message` / `List<Message>` / `Received` /
  `TopicMessage` 를 기준으로 유지한다.
- 다만 send / publish / request / reply 입력 경로에서는, **해당 언어에서 사실상
  표준급이고 copy 감소 효과가 큰 버퍼 생태계 타입**을 adapter surface 로
  지원하는 것을 권장한다.
- 이 지원은 canonical contract 를 대체하지 않는다.
  - recv 결과를 외부 라이브러리 타입으로 바꾸지 않는다.
  - domain object 필드 타입을 외부 라이브러리 타입으로 바꾸지 않는다.
  - 지원하더라도 `Message` 생성 / 입력 adapter / `from_*` helper /
    `impl IntoMultipart` 같은 진입점으로 제한한다.
- 지원 기준:
  - 그 언어의 네트워킹/IO 생태계에서 널리 쓰이는가
  - zero-copy 또는 copy 감소 효과가 실질적인가
  - 특정 프레임워크 종속을 public surface 전체에 강제하지 않는가
- 비기준:
  - niche 라이브러리
  - 특정 회사/프로젝트 내부에서만 주로 쓰는 버퍼 타입
  - canonical type 을 대체하려는 wrapper

권장 우선순위:

| 언어 | 권장 지원 | 수준 | 비고 |
|---|---|---|---|
| Java | Netty `ByteBuf` | Recommended | 네트워크 스택에서 매우 흔하고 direct/off-heap 경로 가치가 큼 |
| Java | Agrona `DirectBuffer` | Optional | 저지연 계열에서 유용하지만 Netty보다 우선순위는 낮음 |
| .NET | `ReadOnlyMemory<byte>` / `ReadOnlySequence<byte>` / `IBufferWriter<byte>` | Recommended | 표준 버퍼 생태계. copy 감소 효과가 큼 |
| .NET | `PipeReader` / `PipeWriter` | Optional | `System.IO.Pipelines` 사용자층에 유용 |
| Rust | `bytes::Bytes` / `BytesMut` | Recommended | async/network 생태계에서 사실상 표준급 |
| Python | buffer protocol / `memoryview` | Recommended | `bytes` / `bytearray` 외 zero-copy 입력 경로 확보 |
| Node | `Buffer` / `Uint8Array` | Baseline | 사실상 기본 지원 범주 |
| Go | `[]byte` / `[][]byte` | Baseline | 언어 기본 경로가 이미 hot path 표준 |

- 설계 규칙:
  - adapter 는 input-side convenience 여야 한다. canonical return type 을
    바꾸지 않는다.
  - 언어 표준 라이브러리나 런타임이 아닌 **third-party buffer type** 은
    가능하면 core binding 이 아니라 별도 extension module 로 분리한다.
    예를 들어 Java `ByteBuffer` 는 core 에 둘 수 있지만, Netty `ByteBuf` 는
    별도 Netty extension 에 두는 방향이 맞다.
  - adapter 지원 여부 때문에 overload 폭이 과도하게 늘어나면 안 된다.
    가능하면 `MessageLike`, `IntoMultipart`, buffer protocol 같은 **한 개의
    통합 진입점**으로 흡수한다.
  - 외부 버퍼 타입을 받더라도 ownership / retain / release 규칙은 바인딩이
    문서로 명확히 정의해야 한다.
  - 프레임워크별 객체 수명 규칙 (`ByteBuf.retain/release`, pooled buffer 등)을
    사용자가 추측하게 두면 안 된다.
  - "지원 가능" 과 "zero-copy 보장" 을 혼동하지 않는다. zero-copy 보장이
    불가능하면 문서에 copy 가능성을 명시한다.

### Codec / Serializer Extension 모듈 정책
- `Message` 와 multipart transport 자체는 계속 canonical binding core contract 다.
- protobuf / json / messagepack codec-aware domain conversion 은
  **binding core 위에 올라가는 정식 별도 extension contract** 로 취급한다.
- 단, `C` binding 은 예외다. `C`는 raw transport contract 를 기본 public surface 로
  유지하며, codec-aware domain conversion 을 기본 binding contract 로 요구하지
  않는다.
- 따라서 `Parse(...)`, `Serialize(...)`, `ToMessage(...)`, `FromMessage(...)`
  같은 helper 를 public 으로 노출할 수 있다. 다만 이 helper 는 binding core
  package/module 에 섞으면 안 된다.
- Required rules:
  - binding core package/module 은 codec-agnostic 해야 한다.
  - binding core 가 protobuf/json/messagepack dependency 를 필수 의존성으로
    끌고 들어오면 안 된다.
  - `C` binding 은 raw byte/message contract 만 정식으로 유지하면 되며,
    protobuf/json helper 를 public contract 로 추가할 의무가 없다.
  - `C`를 제외한 binding 은 codec extension layer 를 public contract 로 두며,
    `protobuf`, `json`, `messagepack` 세 codec 을 지원해야 한다.
  - `C`를 제외한 binding 의 `protobuf`, `json`, `messagepack` extension 은
    각각 **core binding 과 별도 배포 단위** 로 제공해야 한다.
  - third-party buffer adapter extension 도 같은 원칙을 따른다.
    core binding 과 별도 배포 단위로 제공해야 하며, core binding 이 그
    extension dependency 를 필수로 요구하면 안 된다.
  - codec extension 은 core binding 에 의존할 수 있지만, core binding 이 codec
    extension 에 의존하면 안 된다.
  - codec extension 이 추가되어도 canonical recv/request/reply contract 는 계속
    `Message`, `List<Message>`, `Received`, `TopicMessage` 기준으로 유지한다.
  - codec extension 은 object <-> `Message` encode/decode helper 계약만 정의한다.
    payload 타입에 필요한 parser, schema, generated type 입력을 받는 것은 허용된다.
  - codec extension 은 transport 결과 타입을 domain object 로 바꾸는 helper 를
    추가할 수 있지만, raw transport contract 자체를 대체하면 안 된다.
  - codec extension 문서는 packet name 추론 규칙, high-level outbound serializer
    lookup, typed request/reply decode 정책을 정의하지 않는다.
  - framework 가 존재하는 언어에서는 위 정책을 framework 문서가 담당한다.
    codec extension 문서는 low-level encode/decode helper 입력 조건만 설명한다.
- 이유:
  - raw transport 사용자에게 특정 codec dependency 를 강제하지 않기 위함이다.
  - 언어별 codec 생태계 선택이 다르므로 core binding 이 한 구현체에 잠기지
    않게 하기 위함이다.
  - high-level domain helper 와 low-level transport ownership 계약을 분리해서
    변경 파급을 줄이기 위함이다.

JSON codec baseline by language:

| Language | JSON baseline |
|---|---|
| C | none required |
| C++ | `nlohmann/json` |
| .NET | `System.Text.Json` |
| Java | `Jackson` |
| Node | built-in `JSON.parse` / `JSON.stringify` |
| Python | stdlib `json` |
| Go | `encoding/json` |
| Rust | `serde_json` |

- 이 표는 "json codec extension 을 public 으로 노출할 때 기본으로 삼는 구현체"를
  뜻한다.
- 다른 json 라이브러리를 추가 지원할 수는 있다. 다만 public contract 와 sample,
  test, 기본 동작 기준은 위 표를 따른다.
- Node 는 built-in JSON 이 plain object encode/decode 의 기준이며, typed
  validation 은 별도 schema/parser object 위에 얹을 수 있다.

MessagePack codec baseline by language:

| Language | MessagePack baseline |
|---|---|
| C | none required |
| C++ | `msgpack-c` |
| .NET | `MessagePack for C#` |
| Java | `jackson-dataformat-msgpack` |
| Node | `@msgpack/msgpack` |
| Python | `msgpack` |
| Go | `vmihailenco/msgpack/v5` |
| Rust | `rmp-serde` |

Bindings는 더 이상 codec extension 배포 단위를 정의하지 않는다.

| Language | Core binding root | Binding-owned codec package 정책 |
|---|---|---|
| C | `bindings/c/include/zlink/`, `bindings/c/src/` | 없음 |
| C++ | `bindings/cpp/include/zlink/` | 없음. framework 직렬화는 `framework/languages/cpp/extensions/`에서 다룬다 |
| .NET | `bindings/dotnet/src/Zlink/` | 없음. framework 직렬화는 `framework/languages/dotnet/src/`에서 다룬다 |
| Java | `bindings/java/src/main/java/systems/zlink/` | 없음. framework 직렬화는 `framework/languages/java/`에서 다룬다 |
| Node | `bindings/node/src/` | 없음. framework 직렬화는 `framework/languages/node/packages/`에서 다룬다 |
| Python | `bindings/python/src/zlink/` | 없음. raw `Message`/bytes만 유지한다 |
| Go | `bindings/go/` | 없음. raw `Message`/bytes만 유지한다 |
| Rust | `bindings/rust/src/` | 없음. raw `Message`/bytes만 유지한다 |

- 배치 규칙:
  - codec helper source를 core socket/message namespace와 같은 디렉터리에 직접 섞지 않는다.
  - 언어별 codec spec 문서는 raw-only 정책을 설명하고, 해당 언어가 framework target이면
    framework codec extension 위치를 안내한다.
  - binding sample과 test는 raw `Message`/bytes 동작을 검증한다.

### 외부 버퍼 Attach / Release Hook 정책
- C API 의 `zlink_msg_init_data(..., zlink_free_fn*, hint)` 는 **external buffer
  attach + release hook** 능력을 제공한다.
- 바인딩은 이 능력을 **언어 관용구와 메모리 모델에 맞을 때만** public 으로
  노출한다.
- 기본 원칙:
  - **copy-based `Message` 생성 경로는 모든 바인딩에서 Required**
  - **VM 또는 GC 기반 언어(Java, .NET, Go, Python, Node)는 VM-managed
    buffer를 native queue에 borrowed/zero-copy 로 넘기는 public API 를
    제공하지 않는다**
  - **release hook 없는 borrowed zero-copy wrap API 는 managed 언어 public
    surface, default send path, perf 전용 fast path 에 두지 않는다**
  - VM 언어의 성능 경로는 caller buffer 를 native queue 에 빌려주는 방식이
    아니라, native-owned `Message` 를 만들고 그 payload 를 채운 뒤 part 기반
    send/recv API 로 넘기는 방식이어야 한다.
  - external buffer attach 는 **release 시점을 public contract 로 닫을 수 있을
    때만** 허용한다
- 허용:
  - C++
    - `from_external(..., zlink_free_fn*, hint)` 같은 형태로 external attach 허용
    - release hook 이 explicit 하므로 public contract 로 닫을 수 있다
- 비권장/금지:
  - Java / .NET / Go / Rust / Python / Node
    - generic public borrowed wrap (`wrapDirect`, `wrapNative`, `wrap_buffer`
      등) 금지
    - VM-managed buffer 를 `zlink_msg_init_data(..., NULL, NULL)` 로 native
      queue 에 넘기는 send/publish/request/reply fast path 금지
    - VM-managed buffer 를 pin 한 뒤 release callback 으로 풀어 주는 public 또는
      default fast path 금지
    - 이유: send 후 backing buffer lifetime, retain/release, arena/session,
      GC 와의 상호작용을 public contract 로 안전하게 닫기 어렵다
- 예외:
  - C++처럼 caller 가 release hook 과 lifetime 을 명시적으로 소유하는 언어만
    advanced external attach 를 둘 수 있다.
  - VM 또는 GC 기반 언어에서 이 예외를 추가하려면 별도 draft spec, public
    lifetime contract, 회귀 테스트, perf 비교가 먼저 필요하다. 정식 spec 과
    구현에는 바로 추가하지 않는다.

## 경계 비용 정책
- 경계 검증은 가장 이른 안전한 위치에서 한 번 수행하는 것을 우선한다.
- 같은 검증을 여러 레이어에서 반복하면 이유가 명확해야 한다.
- 고정 크기 native struct에 들어가는 값은 truncation 대신 즉시 오류를 반환한다.
- 문자열, topic, routing id, metadata 같은 경계 값은 다음을 함께 고려한다.
  - 길이 상한
  - 인코딩 비용
  - 복사 횟수
  - 재할당 정책
- core의 고정 크기 struct 필드에 대응하는 바인딩 입력의 길이 상한:

  | 필드 | C struct 크기 | 바인딩 검증 책임 |
  |------|--------------|----------------|
  | `RoutingId` | `data[255]` | 값 객체 생성 시 255바이트 초과 시 즉시 오류 반환 |
  | topic / filter | C 문자열 (null-terminated) | 바인딩은 embedded null 문자 포함 시 즉시 오류 반환. 길이 상한은 core가 처리하므로 바인딩에서 별도 길이 검증하지 않는다 |
  | channel_name | `char[256]` | 255바이트 초과 시 즉시 오류 반환 |
  | endpoint | `char[256]` | 255바이트 초과 시 즉시 오류 반환 |
  | metadata | `zlink_msg_t` (가변) | core가 처리, 바인딩은 null 검증만 |

- 바인딩은 고정 크기 필드에 들어가는 값이 상한을 넘으면 truncation 없이
  즉시 예외/오류를 반환한다.
- public 도메인 객체를 만들 때 불필요한 중간 컬렉션 생성은 피한다.
- helper나 sample이 느린 경로를 canonical path처럼 보이게 만들면 안 된다.

## Peer 가중치 정책

peer 가중치는 peer-level outbound 선택 비율과 drain 상태를 제어하는 canonical
surface 다. 모든 바인딩은 구현된 대상 handle에 대해 이를 공개해야 한다.

핵심 API/계약:
- `ZLINK_ROUTER_OPT_WEIGHT`
- `ZLINK_DEALER_OPT_WEIGHT`
- 값 범위 `0..10000`, 기본값 `100`
- submit 결과 `ZLINK_SUBMIT_NOT_ADMITTED` (값 13) — target peer 가중치가 `0`이면 반환
- socket monitor 이벤트 `ZLINK_EVENT_PEER_WEIGHT_CHANGED` (bit 15)
- `zlink_spot_node_peer_entry_t.weight` / `zlink_member_peer_entry_t.weight`

바인딩 규칙:
- `weight`는 언어 관례에 맞는 typed option/property surface로 노출한다.
  설정 대상은 `ROUTER`, `DEALER`이다. `SpotNode`와 `Spot`에는 weight 설정
  surface를 노출하지 않는다.
- `NOT_ADMITTED` 를 `SubmitError` 계열에 포함하여 caller 가
  가중치 `0` 거부를 구분할 수 있게 한다.
- `PEER_WEIGHT_CHANGED` 이벤트 bit 은 기존 socket monitor / service
  monitor surface 에 typed value 로 노출한다. `value`는 새 `0..10000`
  가중치다.
- `SpotNodePeerEntry` / `MemberPeerEntry` 도메인 객체는 `weight` 필드를
  포함해야 한다.

## Monitor 정책
- monitor plane도 같은 규칙을 따른다.
- public monitor receive는 `recv()` 하나로 제공한다.
  - blocking/non-blocking 은 flags 파라미터 또는 언어별 관례로 제어한다.
- monitor event는 data plane과 별도지만, blocking/non-blocking 구분 방식은
  동일해야 한다.
- monitor는 socket의 상태 변화, readiness 변화, lifecycle event를 관찰하는
  별도 plane 이다.
- monitor payload는 message data plane payload와 혼동되면 안 된다.
- monitor event type은 typed event surface 또는 동등한 의미 surface로
  노출해야 한다.
- monitor consumer는 raw integer mask만이 아니라 event 의미를 읽을 수 있어야
  한다.
- monitor lifecycle은 관찰 대상 socket lifecycle과의 관계가 설명 가능해야 한다.
  - monitor open 시점
  - monitor close 시점
  - observed socket close 이후의 동작
- monitor는 data plane을 대체하는 API가 아니다.
- monitor의 readiness/state event 의미는 data plane contract와 충돌하지
  않아야 한다.
- monitor sample과 test는 다음을 보여야 한다.
  - event 수신 성공 경로
  - non-blocking empty 경로
  - socket state 변화와 monitor event의 관계

## 오류 정책

### 바인딩 검증 vs Native 오류
- 입력 값의 형식/범위 오류는 바인딩이 즉시 막는다.
- socket 상태, 연결 상태, transport 상태, protocol 상태 오류는 코어가
  결정하고 바인딩은 그대로 caller에 전달한다.

### 바인딩이 검증해야 하는 항목
- truncation 가능성이 있는 값
- overflow 가능성이 있는 값
- fixed-size native struct에 들어가는 값
- 명백한 길이 상한이 있는 값
- offset/length 범위 오류
- null 불가 인자
- enum 범위 밖의 값

이 경우 바인딩 예외를 사용한다.
- Java: `IllegalArgumentException`, `IndexOutOfBoundsException`,
  `NullPointerException`
- .NET: `ArgumentException`, `ArgumentOutOfRangeException`,
  `ArgumentNullException`
- Go: 즉시 `error` 반환 또는 `panic` (프로그래머 오류)
- Rust: compile-time 보장 (`NonZero`, newtype) 또는 `panic!` / `Result<T, E>`

### Native 가 결정하는 항목
- peer 없음
- backpressure
- readiness 부족
- callback mode와 direct recv 충돌
- socket type/state/runtime 문제
- transport, TLS, endpoint, protocol 오류

이 경우 바인딩은 native 오류를 언어별 관용구로 변환하여 caller에 전달한다.
Exception 언어는 예외를 던지고, return-based 언어는 에러 값을 반환한다.
- C++: `throw zlink_error_t`
- Java: `throw ZlinkException`
- .NET: `throw ZlinkException`
- Node: `throw ZlinkError` (extends `Error`)
- Python: `raise ZlinkError` (extends `Exception`)
- Go: `return err` (`ZlinkError` 또는 동등한 typed error)
- Rust: `Err(E)` (`Result<T, E>`; 여러 함수군이 섞일 때만 `ZlinkError`)

### Error Code 표

zlink 에서 사용하는 코드와 의미. 바인딩은 이 코드를 언어별 에러 타입에
매핑하여 caller 가 원인을 구분할 수 있게 한다.

코드는 두 계층으로 나뉜다.

1. **Public result enum 코드 (0–706)** — 공개 C API 함수의 반환 enum 값.
   바인딩이 직접 마주하고 언어별 에러 타입으로 노출해야 하는 값이다.
   전체 정의는 [core/errno-map.md](https://kairos-code-dev.github.io/zlink/en/spec/core/04-errno-map/) 참조.
2. **Internal errno** — `zlink_errno()` 로 조회되는 내부 raw errno.
   `INTERNAL_ERROR` 같은 coarse bucket 의 상세 원인 조회용. 바인딩은 이 값을
   `internalErrno` / `internal_errno` 필드로 노출한다 (디버깅 전용).

#### Public Result Enum 카탈로그

바인딩은 아래 8 개 enum 의 **모든 값을 누락 없이** 언어별 표현으로 매핑해야
한다. OK (0) 는 모든 enum 에 공통이며 에러로 취급하지 않는다.

##### `zlink_submit_result_t` (send, request submit, reply submit)

| 값 | 상수 | 내부 errno | 분류 | 의미 |
|----|------|-----------|------|------|
| 0 | `OK` | — | 성공 | 제출 성공 |
| 1 | `BACKPRESSURED` | `EAGAIN` | 제어 흐름 | send 큐 포화 (HWM) |
| 2 | `NOT_CONNECTED` | `ENOTCONN`, `EHOSTUNREACH` | 제어 흐름 | 대상 peer/경로 미연결 |
| 3 | `NOT_FOUND` | `ENOENT` | 제어 흐름 | 대상 peer/spot/route 없음 |
| 13 | `NOT_ADMITTED` | `ECONNREFUSED` 계열 | 제어 흐름 | target peer 가중치가 `0`이라 신규 submit 거부 |
| 4 | `TERMINATED` | `ETERM` | 런타임/생명주기 | context 종료됨 |
| 5 | `INVALID_HANDLE` | `EFAULT` | caller 계약 위반 | NULL handle / invalid pointer |
| 6 | `INVALID_ARGUMENT` | `EINVAL` | caller 계약 위반 | 잘못된 인자 |
| 7 | `NOT_SUPPORTED` | `ENOTSUP` | caller 계약 위반 | 해당 소켓 타입에서 지원 안 함 |
| 8 | `INVALID_STATE` | `EFSM`, `EBUSY` | caller 계약 위반 | 소켓/handle 상태 오류 |
| 9 | `THREAD_VIOLATION` | `EMTHREAD` | caller 계약 위반 | 잘못된 스레드에서 접근 |
| 10 | `OUT_OF_MEMORY` | `ENOMEM` | 내부 실패 | 메모리 할당 실패 |
| 11 | `SEQ_EXHAUSTED` | `EBUSY` | 내부 실패 | request seq 공간 고갈 |
| 12 | `INTERNAL_ERROR` | `EPROTO` 등 | 내부 실패 | 내부 submit 실패 (상세는 `zlink_errno()`) |

##### `zlink_request_result_t` (request completion callback)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | `0` | reply payload 수신 성공 |
| 101 | `TIMED_OUT` | `ETIMEDOUT` | `timeout_ms` 내 reply 미도착 |
| 102 | `NOT_FOUND` | `ENOENT` | 대상 없음, 에러 reply 로 완료 |
| 103 | `TERMINATED` | `ETERM` | (예약) 명시적 종료 완료 경로 |
| 104 | `PROTOCOL_ERROR` | `EPROTO` | reply envelope / error reply payload 손상 |
| 105 | `INTERNAL_ERROR` | `EPROTO` 등 | 내부 request 실패 (상세는 `zlink_errno()`) |
| 106 | `REJECTED` | `EACCES`, `ECONNREFUSED` | 대상이 request를 명시적으로 거절 |
| 107 | `CONFLICT` | `ESTALE` | request 대상 또는 상태 충돌 |
| 108 | `BUSY` | `EBUSY` | request 처리 경로가 일시적으로 바쁨 |
| 109 | `NOT_CONNECTED` | `ENOTCONN`, `EHOSTUNREACH` | 대상 peer/경로 미연결 |
| 110 | `INVALID_ARGUMENT` | `EINVAL`, `EFAULT` | request 인자 또는 envelope 오류 |
| 111 | `INVALID_STATE` | `EFSM` | request를 받을 수 없는 handle 상태 |
| 112 | `NOT_SUPPORTED` | `ENOTSUP`, `EOPNOTSUPP` | request 미지원 대상 |

##### `zlink_recv_result_t` (recv, subscribe, subscription event, monitor recv, timer recv)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | 수신 성공 |
| 201 | `NO_DATA` | `EAGAIN` | non-blocking recv 데이터 없음 / source 고갈 |
| 202 | `BUSY` | `EBUSY` | handler 이미 attach 됨 |
| 203 | `TERMINATED` | `ETERM` | context 종료됨 |
| 204 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |
| 205 | `NOT_SUPPORTED` | `ENOTSUP` | recv 미지원 소켓 타입 |
| 206 | `INTERNAL_ERROR` | `EPROTO` 등 | 내부 recv 실패 (상세는 `zlink_errno()`) |

##### `zlink_handler_result_t` (handler 등록)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | handler 등록 성공 |
| 301 | `INVALID_ARGUMENT` | `EINVAL` | NULL handler |
| 302 | `BUSY` | `EBUSY` | handler 이미 attach 됨 |
| 303 | `NOT_SUPPORTED` | `ENOTSUP` | 미지원 subject |
| 304 | `DEADLOCK` | `EDEADLK` | reentrant 호출 (send-ready handler 전용) |
| 305 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |
| 306 | `INTERNAL_ERROR` | `EPROTO` 등 | 내부 handler 등록 실패 (상세는 `zlink_errno()`) |

##### `zlink_close_result_t` (close, destroy)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | close/destroy 성공 |
| 401 | `BUSY` | `EBUSY` | in-flight callback / API 호출 |
| 402 | `SHUTDOWN` | `ESHUTDOWN` | 이미 close 됨 |
| 403 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |
| 404 | `INTERNAL_ERROR` | `EPROTO` 등 | 내부 close 실패 (상세는 `zlink_errno()`) |

##### `zlink_bind_result_t` (bind)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | bind 성공 |
| 501 | `INVALID_ARGUMENT` | `EINVAL` | 잘못된 endpoint |
| 502 | `ADDR_IN_USE` | `EADDRINUSE` | 주소 이미 사용 중 |
| 503 | `NOT_SUPPORTED` | `ENOTSUP` | 미지원 transport |
| 504 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |
| 505 | `INTERNAL_ERROR` | `EPROTO` 등 | 내부 bind 실패 (상세는 `zlink_errno()`) |

##### `zlink_connect_result_t` (connect, disconnect, unbind)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | connect/disconnect/unbind 성공 |
| 601 | `INVALID_ARGUMENT` | `EINVAL` | 잘못된 endpoint |
| 602 | `NOT_SUPPORTED` | `ENOTSUP` | 미지원 transport |
| 603 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |
| 604 | `INTERNAL_ERROR` | `EPROTO` 등 | 내부 connect/disconnect 실패 (상세는 `zlink_errno()`) |
| 605 | `NOT_FOUND` | `ENOENT` | endpoint 또는 peer routing id 없음 |
| 606 | `CONFLICT` | `EADDRINUSE` | peer routing id가 둘 이상의 pipe와 충돌 |
| 607 | `BUSY` | `EBUSY` | lifecycle owner가 수동 변경을 거절 |

##### `zlink_config_result_t` (option set/get, message lifecycle, snapshot, poller mutation, proxy, timer config)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | 설정 성공 |
| 701 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |
| 702 | `INVALID_ARGUMENT` | `EINVAL`, `EBUSY` | 잘못된 인자 또는 config 계층 conflict |
| 703 | `NOT_SUPPORTED` | `ENOTSUP` | 미지원 옵션 |
| 704 | `INTERNAL_ERROR` | `EPROTO` 등 | 내부 config 실패 (상세는 `zlink_errno()`) |
| 705 | `INVALID_STATE` | `EBUSY`, `ESHUTDOWN` | lifecycle 상태가 config를 거절 |
| 706 | `NOT_FOUND` | `ENOENT` | local lookup 대상 없음 |

##### Non-OK 값 총합

- 총 **59 개** non-OK 코드 (submit 13 + request 12 + recv 6 + handler 6 +
  close 4 + bind 5 + connect 7 + config 6 = 59). 값 범위:
  1–13, 101–112, 201–206, 301–306, 401–404, 501–505, 601–607, 701–706.
- 값 범위는 enum 간 겹치지 않으므로 단일 `int` 로 유일하게 구분된다.
- 바인딩은 59 개 값 모두에 대해 언어별 에러 표현을 제공해야 한다. 누락 시
  caller 가 해당 원인을 구분할 방법이 없다.

언어별 enum/상수 매핑 스타일은 아래 `언어별 ErrorCode 매핑` 절을 참조한다.

#### POSIX 표준 errno

POSIX 에서 해당 상수가 정의되지 않은 플랫폼에서는 `ZLINK_HAUSNUMERO` 기반
대체 값을 사용한다. 바인딩은 상수 이름으로 비교해야 하며 정수 값에 직접
의존하면 안 된다.

| errno | 대체 값 (POSIX 미정의 시) | 의미 | 대표 발생 상황 |
|-------|-------------------------|------|--------------|
| `ENOTSUP` | HAUSNUMERO + 1 | 지원하지 않는 작업 | 해당 소켓 타입에서 불가능한 작업 |
| `EPROTONOSUPPORT` | HAUSNUMERO + 2 | 프로토콜 미지원 | 지원하지 않는 프로토콜 요청 |
| `ENOBUFS` | HAUSNUMERO + 3 | 버퍼 공간 부족 | 내부 버퍼 할당 실패 |
| `ENETDOWN` | HAUSNUMERO + 4 | 네트워크가 다운됨 | transport 레이어 장애 |
| `EADDRINUSE` | HAUSNUMERO + 5 | 주소가 이미 사용 중 | bind 시 endpoint 충돌 |
| `EADDRNOTAVAIL` | HAUSNUMERO + 6 | 주소를 사용할 수 없음 | 잘못된 endpoint 형식 |
| `ECONNREFUSED` | HAUSNUMERO + 7 | 연결 거부됨 | 대상이 연결을 거부 |
| `EINPROGRESS` | HAUSNUMERO + 8 | 작업 진행 중 | 비동기 연결 진행 중 |
| `ENOTSOCK` | HAUSNUMERO + 9 | 소켓이 아닌 대상 | 잘못된 handle 전달 |
| `EMSGSIZE` | HAUSNUMERO + 10 | 메시지 크기 초과 | 메시지가 설정된 최대 크기 초과 |
| `EAFNOSUPPORT` | HAUSNUMERO + 11 | 주소 체계 미지원 | 지원하지 않는 주소 체계 |
| `ENETUNREACH` | HAUSNUMERO + 12 | 네트워크에 도달 불가 | 라우팅 불가 |
| `ECONNABORTED` | HAUSNUMERO + 13 | 연결이 중단됨 | 연결이 비정상 종료 |
| `ECONNRESET` | HAUSNUMERO + 14 | 연결이 재설정됨 | peer 가 연결을 강제 종료 |
| `ENOTCONN` | HAUSNUMERO + 15 | 연결되지 않은 상태 | 연결 전에 send/recv 시도 |
| `ETIMEDOUT` | HAUSNUMERO + 16 | 작업 시간 초과 | request reply timeout, 연결 timeout |
| `EHOSTUNREACH` | HAUSNUMERO + 17 | 대상에 도달할 수 없음 | peer 미연결, 라우팅 불가 |
| `ENETRESET` | HAUSNUMERO + 18 | 네트워크가 재설정됨 | 네트워크 연결 끊김 |
| `EAGAIN` | (POSIX 표준) | 자원이 일시적으로 사용 불가 | non-blocking send 시 HWM 도달 (backpressure) |
| `EINVAL` | (POSIX 표준) | 잘못된 인자 | 범위 초과, 잘못된 옵션 값 |
| `ECANCELED` | (POSIX 표준) | 작업이 취소됨 | caller 가 request 를 취소 |

`ZLINK_HAUSNUMERO` 값은 `156384712` 이다.

#### zlink 전용 errno

zlink 고유 오류 코드. POSIX errno 와 충돌하지 않도록 `ZLINK_HAUSNUMERO`
기반 오프셋을 사용한다.

| 대체 값 | 상수 | 의미 | 대표 발생 상황 |
|--------|------|------|--------------|
| HAUSNUMERO + 51 | `EFSM` | 유한 상태 기계 오류 | 소켓 상태에서 허용되지 않는 작업 (예: callback 모드에서 direct recv) |
| HAUSNUMERO + 52 | `ENOCOMPATPROTO` | 호환되지 않는 프로토콜 | 서로 다른 프로토콜 버전의 peer 연결 |
| HAUSNUMERO + 53 | `ETERM` | 컨텍스트/소켓 종료 | context 또는 소켓이 close 된 상태에서 작업 시도 |
| HAUSNUMERO + 54 | `EMTHREAD` | I/O 스레드 부족 | context 의 I/O 스레드가 부족 |

#### 언어별 ErrorCode 매핑

각 바인딩은 Public Result Enum 카탈로그의 59 개 non-OK 코드를 언어별
enum/상수로 매핑하여 타입 안전한 분기를 제공한다.

| 언어 | 처리 | ErrorCode 타입 | 접근 방식 |
|------|------|---------------|----------|
| C | return | 함수별 typed enum (`zlink_*_result_t`) | 반환값 자체 |
| C++ | throw | 통합 `ErrorCode` enum | `zlink_error_t.code()` |
| Java | throw | 통합 `ErrorCode` enum | `ZlinkException.getCode()` |
| .NET | throw | 통합 `ErrorCode` enum | `ZlinkException.Code` |
| Node | throw | 통합 `ErrorCode` enum (또는 string 상수) | `ZlinkError.code` |
| Python | throw | 통합 `ErrorCode` enum | `ZlinkError.code` |
| Go | return | 통합 `ErrorCode` typed int 상수 | `ZlinkError.Code()` |
| Rust | return (`Result`) | 통합 `ErrorCode` enum variant | `ZlinkError.code()` |

- 통합 enum 의 각 variant 는 Public Result Enum 카탈로그의 59 개 값과
  1:1 대응한다. 원본 C 의 enum 분리 (submit / recv / handler / close /
  bind / connect / config / request) 를 유지하거나, 언어 관용구에 따라
  단일 enum 으로 통합해도 된다. 둘 중 어떤 스타일이든 **값은 누락 없이 모두
  표현해야 한다**.
- 상수/variant 이름은 원본 `UPPER_SNAKE_CASE` 를 그대로 쓰거나 언어 스타일
  (`PascalCase` / `camelCase`) 로 변환한다. 숫자 값과 의미는 고정이다.
- `internalErrno` / `internal_errno` 필드는 별도로 제공하며, 주로
  `INTERNAL_ERROR` 같은 coarse bucket 의 상세 원인 조회용이다.

### Request-Reply 오류 정책

request-reply 는 Per-Function Error Type Hierarchy 의 **`RequestError`**
(request completion) 과 **`SubmitError`** (request submit) 두 하위 타입을
사용한다. `RequestError` 는 `zlink_request_result_t` 에 대응하며,
`SubmitError` 는 `zlink_submit_result_t` 에 대응한다.

오류 코드는 두 계층으로 나뉜다.

**Wire error reply 코드** — peer 가 보내는 protocol-level error reply.
wire 에서 사용 가능한 errno 는 3개로 제한된다: `ENOENT`, `EOPNOTSUPP`, `EINVAL`.

**API/completion 코드** — core 가 callback 에 전달하는 errno:

| errno | 발생 시점 |
|-------|----------|
| `ENOENT` | 대상 peer/spot 을 찾지 못함 (wire 또는 local) |
| `EOPNOTSUPP` | peer 종류 불일치 또는 지원 안 함 |
| `EINVAL` | 잘못된 파라미터 |
| `ETIMEDOUT` | reply 대기 중 timeout 초과 |
| `EPROTO` | envelope parse 실패 또는 잘못된 remote reply |
| `EBUSY` | 수신 표면 충돌 (handler 중복 등록) |

**request 오류 (`RequestError`):**

| 상황 | `request()` |
|------|------------|
| backpressure | writable 대기 (timeout 에 합산) |
| timeout | `RequestError(TIMED_OUT)` |
| 대상 없음 | `RequestError(NOT_FOUND)` |
| remote error reply | `RequestError(해당 코드)` |
| 소켓 close | `RequestError(TERMINATED)` |
| protocol error | `RequestError(PROTOCOL_ERROR)` |
| pending map 에 없는 reply | 무시 |

**reply 오류 (`SubmitError`):**

| 상황 | `reply()` |
|------|-----------|
| 성공 | 정상 반환 |
| backpressure | `SubmitError(BACKPRESSURED)` |
| not connected | `SubmitError(NOT_CONNECTED)` |
| 기타 실패 | `SubmitError(해당 submit 코드)` |

- async request 는 완료 실패를 async completion 경로 (Future reject / await
  error) 로 전달한다.
- callback request 는 **submit 실패를 즉시 throw/return** 하고, submit 성공 후의
  완료 실패만 callback 의 `RequestResult` / `RequestError` 로 전달한다.
- 함수군별 하위 에러 타입을 사용한다 (Per-Function Error Type Hierarchy 참조).
  - submit 실패: `SubmitException` / `SubmitError`
  - request 완료 실패: `RequestException` / `RequestError`
- 언어별 표현:
  - Java: `SubmitException` / `RequestException` — `getCode()` 로 원인 구분 (unchecked)
  - .NET: `ZlinkSubmitException` / `ZlinkRequestException` — `Code` property
  - Node: `SubmitError` / `RequestError` — `code` property
  - Python: `SubmitError` / `RequestError` — `code` attribute
  - C++: `submit_error_t` / `request_error_t` — `.code()` 메서드
  - Go: `*SubmitError` / `*RequestError` — `Code()` 메서드 (interface)
  - Rust: `Err(SubmitError{..})` / `Err(RequestError{..})`, 또는 다중 함수군
    경계에서는 `Err(ZlinkError::Submit(..))` / `Err(ZlinkError::Request(..))`
    — `.code()` 메서드

## 길이와 범위 경계 정책
- 검증 책임은 두 층으로 나눈다.
- 값 객체가 존재하는 타입:
  - 값 객체 생성 시점에 canonical validation을 수행한다.
  - 예: `RoutingId`, typed enum wrapper, bounded identifier
- 값 객체가 존재하지 않거나 호출 문맥 의존 변환이 필요한 타입:
  - native 호출 직전에 검증한다.
  - 예: `Duration -> int millis`, offset/length slicing, output buffer sizing
- native 호출 직전 재검증은 아래 경우에만 필수다.
  - 값 객체를 거치지 않는 raw 경로가 존재하는 경우
  - 값 객체 생성 후 호출 직전 추가 변환이 들어가는 경우
  - 값 객체가 아닌 복합 입력 조합에서 overflow/truncation이 생길 수 있는 경우
- truncation 후 native로 넘기는 동작은 금지한다.

예:
- `RoutingId`는 `zlink_routing_id_t`의 `data[255]` 계약을 넘기지 않아야 한다.
- `Duration -> int millis` 변환은 overflow를 허용하면 안 된다.
- topic, subscription, metadata처럼 고정 출력 버퍼가 개입되는 경로는 길이와
  재할당 정책이 명확해야 한다.

## 소유권 정책
- `Message` ownership은 코어 계약과 일치해야 한다.
- 모든 바인딩은 내부적으로 C API를 호출하므로, GC 언어를 포함한 전 언어에서
  native message의 ownership을 올바르게 관리해야 한다.
- ownership 경로:
  - send 성공: ownership이 native로 이동한다. 바인딩은 이후 접근하면 안 된다.
  - send 실패: restore 가능한 경로와 consume되는 경로를 혼동하지 않는다.
  - recv: native가 생성한 메시지의 ownership을 바인딩이 넘겨받는다. 바인딩이
    해제 책임을 진다.
  - 생성 후 미전송: 바인딩이 직접 생성한 메시지를 전송하지 않았다면 반드시
    명시적으로 close/해제해야 한다. GC가 managed wrapper만 수거할 뿐, native
    메모리는 해제하지 않으므로 누수가 발생한다.
- callback delivery와 direct receive는 동일한 payload shape를 가져야 한다.
- callback 후 frame validity는 계약으로 명확해야 한다.

## 네이밍 정책
- 메서드명은 언어 관례만 반영한다.
- 개념 이름은 바인딩 간 최대한 동일하게 유지한다.
- 아래 목록은 의미 기준 canonical name 이다.
- 실제 바인딩 메서드명은 다음 세 가지 변형만 허용한다.
  1. **케이싱 변형**: 언어 관례에 맞게 camelCase/PascalCase/snake_case를
     변환한다. 단어 구성은 바뀌지 않는다.
     - 예: `connectPeer` → Go: `ConnectPeer`, Python: `connect_peer`,
       C++: `connect_peer`, Rust: `connect_peer`
  2. **overload 불가 언어의 최소 접미사**: Go와 Rust처럼 overloading이 없는
     언어에서, 동일 동작의 파라미터 변형을 구분하기 위해 최소한의 접미사를
     허용한다. 이 접미사는 동작 구분이며, 파라미터 인코딩이 아니다.
     - 예: `send` → Go: `Send` / `SendTo`, Rust: `send` / `send_to`
     - 허용 접미사 범위: `To` 수준의 최소 동작 구분 접미사까지만 허용한다.
       파라미터 타입이나 의미를 풀어쓴 접미사는 금지한다.
       - 허용: `SendTo`, `send_to`
       - 금지: `SendWithRoutingId`, `send_routed`, `send_multipart`
     - 접미사 허용은 overloading도 keyword/optional parameter도 없는
       언어(Go, Rust)에만 적용된다.
     - 접미사 없이 시그니처로 구분 가능한 언어에서는 접미사를 사용하지
       않는다.
       - overloading: Java, C#, C++
       - keyword / optional parameter: Python
       - optional / union type: Node/TypeScript
  3. **언어별 property/getter 관례**: 값을 읽는 accessor는 언어 관례에 맞는
     property 또는 getter 형태를 사용할 수 있다. 단, 개념 이름은 같아야 하고
     새로운 동작 이름을 만들면 안 된다.
     - 예: canonical `getValue` → C++ `value()`, .NET `Value` 또는
       `GetValue()`, Java/Node `getValue()`
     - 예: canonical `routingId`/`getRoutingId` → C++ `routing_id()`,
       Java `routingId()`, Node `getRoutingId()`
- **그 외의 단어 교체, 단어 생략, 다른 단어 대체는 허용하지 않는다.**
  - 금지 예: `setDispatchHandler`를 `spotDispatchHandler`로 바꾸는 것 → 단어 교체
  - 금지 예: `querySnapshot`을 `snapshot`으로 줄이는 것 → 단어 생략이므로,
    canonical 이름 자체를 `snapshot`으로 정의해야 한다
- 케이싱이나 접미사가 달라져도 역할 구분과 의미 계약은 같아야 한다.
- 예: `receiveSubscriptionEvent` → Python: `receive_subscription_event`,
  Go: `ReceiveSubscriptionEvent`
- 추천 canonical 이름:
  - `bind`, `connect`, `close`
  - `send`
  - `recv`
  - `publish`
  - `subscribe`
  - `receiveSubscriptionEvent`
  - `setSubscription`, `unsetSubscription`
  - `setPacketHandler`, `setDispatchHandler`, `setSendReadyHandler`

### 메서드 이름 간결성
- 이 규칙은 public API에 엄격히 적용한다.
- internal/private API는 파라미터 인코딩이 가독성을 높이면 허용한다.
  - 내부 코드는 overloading 없이 명시적 이름이 더 읽기 좋을 수 있다.
  - 예: internal helper에서 `sendRouted(id, msg)`는 허용
- 메서드 이름은 동작(action)만 표현한다.
- 파라미터의 존재, 타입, 개수를 이름에 반복하지 않는다.
- 시그니처가 이미 설명하는 것을 이름에 다시 쓰면 안 된다.
- 동작 자체가 다른 경우(예: `send` vs `publish`)는 이름이 달라야 한다.
- 입력만 다른 경우(예: routing id 유무)는 이름을 늘리지 않는다.

안티패턴과 올바른 패턴:

| 안티패턴 | 올바른 패턴 | 이유 |
|---|---|---|
| `send(message)` | `send().message(message).submit()` | 시작점은 전송 대상만 받고 payload는 builder 단계로 분리 |
| `sendWithRoutingId(id, msg)` | `send(id).message(msg).submit()` | builder가 RoutingId와 payload를 단계로 분리 |
| `sendMultipartMessages(parts)` | `send().message(p1).message(p2).submit()` | builder의 `.message(...)` 반복으로 multipart 표현 |
| `publish(topic, message)` | `publish(topic).message(message).submit()` | topic과 payload를 한 시작점에 섞지 않음 |
| `publishToTopic(topic, msg)` | `publish(topic).message(msg).submit()` | publish는 topic이 있는 동작, builder가 payload를 단계로 분리 |
| `sendToChannel(channel, message)` | `sendToChannel(channel).message(message).submit()` | channel 대상과 payload를 builder 단계로 분리 |
| `requestToChannel(channel, parts, timeout)` | `requestToChannel(channel).message(p1).message(p2).timeout(timeout).submit()` | channel request의 payload와 timeout은 builder 단계 |
| `requestFrame(seq, parts)` | public 표면 금지 | request sequence와 frame layout은 runtime/internal helper 세부사항 |
| `dealer.reply(token, parts)` | `received.reply().message(...).submit()` 또는 router/SPOT reply | DEALER는 특정 peer routing id를 지정할 수 없어 임의 token reply가 개념적으로 맞지 않음 |
| `recvWithTimeout(timeout)` | `recv(timeout)` | 시그니처로 충분 |
| `setLingerTimeoutMilliseconds(ms)` | `setLinger(duration)` | 타입이 단위를 전달 |

송신·요청·응답·게시·Actor 표면은 `Operation Builder Policy` 에 따라 builder
시작점만 노출하고, payload·flags·timeout·callback 등 모든 변형 축은 builder
단계로 표현한다. 시작점 이름은 동작(action)만 담고 파라미터의 존재, 타입,
개수를 이름에 반복하지 않는다.

비-builder public 표면(예: snapshot, lookup, getter/setter) 에서 파라미터
조합이 다를 때 이름을 늘리는 대신 각 언어의 고유 disambiguation 메커니즘을
사용한다.

- Java / C# / C++: overloading
  - 이름은 하나, 시그니처가 구분
- Go: 가변 인자 / functional option / 별도 메서드
  - overloading이 없으므로 동작 의미가 다른 경우에만 최소 접미사를 허용한다
  - 파라미터를 그대로 이름에 넣지 않는다
- Python: keyword argument / optional parameter
  - 이름은 하나, keyword가 구분
- Node/TypeScript: optional parameter / union type
  - 이름은 하나, 타입이 구분
- Rust: trait bound / `Option<T>` / newtype
  - overloading이 없으므로 `impl Into<T>`, `Option<T>`, strong newtype으로 구분
  - 동작 의미가 다른 경우에만 최소 접미사를 허용한다
  - 파라미터를 그대로 이름에 넣지 않는다

언어별 정리:

| 언어 | disambiguation 방식 | 이름에 파라미터 인코딩 |
|---|---|---|
| Java | overloading | 금지 |
| C# | overloading | 금지 |
| C++ | overloading + strong type | 금지 |
| Go | 별도 메서드 / functional option | 금지, 동작 구분 접미사만 허용 |
| Python | keyword / optional | 금지 |
| Node/TS | optional / union | 금지 |
| Rust | trait bound / Option / newtype | 금지, 동작 구분 접미사만 허용 |

## 호환성 정책
- 호환성보다 일관된 public surface를 우선할 수 있다.
- deprecated compatibility layer는 가능한 빨리 제거한다.
- canonical path 외에 동일 기능의 우회 표면을 public 으로 함께 두지 않는다.
- flag 타입 정책:
  - public flags 노출 여부와 형태는 위 `Flags Policy` 절을 따른다.
  - .NET의 `SendFlags` / `RecvFlags` public surface는 canonical 계약이다.
  - 언어별 spec에 없는 legacy flag 타입이나 중복 flag 경로는 추가하지 않는다.

## 언어 간 정렬

### 공유 동작 계약
- blocking send/receive 계열은 실패 시 언어별 에러 경로 (exception 언어는
  예외, return-based 언어는 에러 반환)
- non-blocking receive 는 "데이터 없음"도 동일한 에러 경로로 전달
  (result code 로 구분). 별도 `try*` API 는 제공하지 않는다.
- non-blocking send 는 explicit outcome (submit result code)
- multipart-only
- typed option surface

### 언어별 반환 스타일
- C API
  - raw contract와 함수별 typed result enum
  - multipart-only 기준 surface
  - blocking API + explicit non-blocking entry (`flags` 파라미터)
- C++
  - RAII와 typed wrapper
  - multipart-only 기준 surface
  - 실패는 `throw zlink_error_t` (`SubmitResult` 코드 포함)
- .NET
  - typed option surface + `ZlinkException`
  - multipart-only 기준 surface
  - 실패는 `throw ZlinkException` (`Code` 포함)
- Java
  - domain object + `ZlinkException`
  - multipart-only 기준 surface
  - 실패는 `throw ZlinkException` (`getCode()` 포함)
- Go
  - `(T, error)` + strong type + explicit error check
  - multipart-only 기준 surface
  - 모든 실패는 `error` 반환 (`SubmitResult` 코드 포함)
- Rust
  - `Result<T, E>` + strong newtype + ownership
  - multipart-only 기준 surface
  - 단일 함수군은 `BindError` / `SubmitError` 같은 concrete error,
    다중 함수군은 `ZlinkError`
- Node/Python
  - 언어 관례를 따르되 의미 계약은 동일
  - multipart-only 기준 surface
  - 모든 실패는 `throw` / `raise` (`SubmitResult` 코드 포함)

언어별 표면은 달라도 의미 계약은 같아야 한다.

### 언어 간 Capability 표 (Target)
이 표는 `.NET` 기준으로 정리한 target 역할 표다. 이미 구현된 바인딩의 현재
public surface가 이 표와 다르면, 해당 항목은 구조 정렬 또는 breaking cleanup 작업의
목표로 해석한다. 단, `Internal-only` 항목은 target 상태에서도 public API, sample,
guide, spec signature에 노출하지 않는다.

| Area | C API | C++ | .NET | Java | Go | Rust | Node | Python |
|---|---|---|---|---|---|---|---|---|
| Multipart-only public surface | Required | Required | Required | Required | Required | Required | Required | Required |
| Blocking API named directly | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Non-blocking receive uses flags + empty result | C raw `DONTWAIT` | Required | Required | Required | Required | Required | Required | Required |
| Non-blocking send explicit outcome | Core enum/result | Required | Required | Required | Required | Required | Required | Required |
| Public flags surface | Raw C flags | `int flags` | `SendFlags` / `RecvFlags` | `SendFlags` overload | `flags SendFlags` | `SendFlags` via `.flags(...)` builder step | `flags?: SendFlags` | keyword `flags` |
| Typed option surface | N/A raw C options | Required | Required | Required | Required | Required | Required | Required |
| Socket TLS helpers | `zlink_set_tls_*` | Required | Required | Required | Required | Required | Required | Required |
| Service TLS helpers | `zlink_set_tls_*` on service handles | Required | Required | Required | Required | Required | Required | Required |
| Socket Capability Matrix 준수 | Core 기준 | Required | Required | Required | Required | Required | Required | Required |
| `onReceive` callback | STREAM raw fn ptr | Internal-only | Internal-only | Internal-only | Internal-only | Internal-only | Internal-only | Internal-only |
| `setPacketHandler` callback registration | STREAM packet fn ptr | Required | Required | Required | Required | Required | Required | Required |
| `setDispatchHandler` callback registration | SPOT raw fn ptr | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required |
| `recvActorLifecycle` | SPOT lifecycle queue | Required | Required | Required | Required | Required | Required | Required |
| `setSendReadyHandler` callback registration | Raw fn ptr | Required | Required | Required | Required | Required | Required | Required |
| StreamSocket `connect` 차단 | N/A | Required | Required | Required | Required | Required | Required | Required |
| StreamSocket `disconnectRid` 차단 | N/A | Required | Required | Required | Required | Required | Required | Required |
| Public `detachStream` 비노출 | N/A | Required | Required | Required | Required | Required | Required | Required |
| Poller result type name | N/A | `poll_event_t` | `PollEvent` | `PollEvent` | `PollEvent` | `PollEvent` | `PollEvent` | `PollEvent` |
| Monitor typed event surface | Raw struct | Required | Required | Required | Required | Required | Required | Required |

## 테스트 정책
바인딩 테스트의 목적은 언어별 테스트 개수를 맞추는 것이 아니다. 목적은 각
바인딩이 자기 public surface에 해당하는 contract를 빠짐없이 같은 수준으로
보장하는지 확인하는 것이다.

테스트 수는 언어별 API 표면, 런타임 ownership 모델, 패키징 방식에 따라 달라질 수
있다. 따라서 테스트 개수는 판단의 보조 신호일 뿐이고, 아래 검증 계층과 Test
Matrix의 의미 계약을 충족하는지가 실제 기준이다. 특정 바인딩의 테스트가 유난히
많거나 적으면 개수 자체를 맞추기보다, 누락된 contract가 있는지 또는 core
correctness 재검증 같은 중복 테스트가 섞였는지를 먼저 확인한다.

zlink 바인딩은 native 함수를 단순 호출하는 얇은 래퍼가 아니다. 각 언어 바인딩은
public facade, helper object, domain object, typed option, callback delivery,
ownership 관리, native loader, package boundary, hot path 최적화를 함께 제공한다.
따라서 테스트도 단순 roundtrip만으로 충분하지 않다. public helper가 제공하는
추가 의미와 최적화 불변식도 binding contract의 일부로 검증해야 한다.

테스트는 아래 계층으로 분류한다.

- `Required`: 모든 바인딩이 반드시 가져야 하는 테스트다.
- `Conditional`: 해당 public API나 배포 단위를 제공하는 바인딩만 반드시 가져야
  하는 테스트다.
- `Language-specific`: 특정 런타임의 수명, 예외, GC, borrow, cgo, native loader
  같은 위험을 검증하는 테스트다. 다른 언어에 억지로 복제하지 않는다.
- `Sample smoke`: 사용자-facing 패턴이 public API로 실행되는지 확인하는 테스트다.
  core correctness를 다시 검증하는 대량 시나리오로 확장하지 않는다.
- `Out of scope`: core 자체의 메시징 correctness, transport matrix 전체 재검증,
  일회성 migration 검증, 자동화할 수 없는 리뷰 항목이다. 이런 항목은 영구
  바인딩 테스트로 남기지 않는다. 다만 바인딩 helper, facade, 최적화 불변식이
  관여하는 경로라면 core 기능과 겹쳐 보여도 바인딩 테스트로 유지한다.

공통 원칙은 아래와 같다.

- public surface test로 canonical public API를 고정한다.
- contract test로 바인딩과 native 경계의 타입 변환, 오류 매핑, handle lifecycle을
  검증한다.
- behavior test로 바인딩 public API가 core 계약을 올바르게 중계하는지 검증한다.
- helper/facade test로 바인딩이 추가로 제공하는 언어 친화 기능의 의미 계약을
  검증한다.
- ownership 테스트는 send 성공, send 실패, receive, callback, multipart 경로를
  모두 포함해야 한다.
- optimization guard test로 hot path가 정책에서 금지한 느린 경로로 퇴행하지
  않았는지 검증한다.
- callback mode와 direct mode가 함께 허용되지 않는 경로는 충돌 규칙을 검증한다.
- option 테스트는 typed option surface와 잘못된 역할 접근 차단을 함께
  검증한다.
- 성능 회귀 검증은 별도 Perf Policy가 담당한다. 기능 테스트가 perf benchmark를
  대체하거나, perf benchmark가 public contract test를 대체하면 안 된다.

테스트 충족 기준은 아래와 같다.

- 각 바인딩은 자신이 제공하는 public API에 대해 Test Matrix의 `Required` 항목을
  모두 검증해야 한다.
- 특정 public API, extension package, sample suite를 제공하면 대응하는
  `Conditional` 항목도 검증해야 한다.
- 언어 런타임 때문에 생기는 ownership, lifetime, loader, callback, GC, borrow,
  cgo 같은 위험은 `Language-specific` 테스트로 검증한다.
- 제공하지 않는 public API에 대한 테스트를 개수 맞추기 목적으로 추가하지 않는다.
- core correctness를 다시 검증하는 테스트는 바인딩 helper, facade, package
  boundary, native loader, 최적화 불변식과 직접 관련이 없으면 바인딩 테스트에서
  제거하거나 core test로 옮긴다.
- 같은 계약을 여러 테스트가 반복해서 검증하면 하나의 깊은 테스트로 합치고,
  서로 다른 계약을 한 테스트가 숨기고 있으면 Matrix 항목이 드러나도록 나눈다.

정책 변경 시 필수 테스트 규칙:

- public surface 변경: public surface test 동반
- contract 계약 변경: contract test 동반
- blocking/non-blocking 계약 변경: behavior test 동반
- ownership/receive shape 변경: callback regression 또는 ownership test 동반
- option surface 변경: typed option surface test와 negative 역할 test 동반
- codec extension 변경: 해당 codec extension test 동반
- helper/facade 변경: helper/facade contract test 동반
- hot path 구현 변경: optimization guard test 또는 perf regression gate 동반

기존 코드에 Test Matrix 바깥의 테스트가 있으면 아래 기준으로 정리한다.

- core 기능 재검증이면 core test로 옮기거나 삭제한다.
- migration 검증이면 migration 완료 후 삭제할 임시 테스트로 표시한다.
- 사용자-facing 패턴 확인이면 sample smoke로 이동한다.
- 바인딩 helper, facade, package boundary, native loader, 최적화 불변식 검증이면
  Test Matrix의 적절한 카테고리로 분류해서 유지한다.
- 특정 언어 런타임 위험을 검증한다면 Language-specific 테스트로 남기고, 이유를
  테스트 이름이나 파일 이름에서 알 수 있게 한다.

### 테스트 실행 스크립트 정책
- 각 바인딩은 전체 테스트를 한번에 실행할 수 있는 스크립트를 제공해야 한다.
- 실행 스크립트는 `bindings/<언어>/tests/` 디렉토리에 위치해야 한다.
- 스크립트는 반복 실행 가능하고 성공/실패를 요약해서 보여줘야 한다.
- 권장 형태:
  - `tests/run_tests.sh`
  - `tests/run_tests.ps1`
  - language-specific test runner entry

### 버그 발견 정책
- 테스트 또는 perf 작성/실행 중 버그를 발견한 경우 다음 절차를 따른다.
- 바인딩 라이브러리 버그:
  - 해당 바인딩에서 직접 수정한다.
  - 수정과 함께 회귀 테스트를 추가한다.
- core 라이브러리 버그:
  - 바인딩에서 core 버그를 직접 수정하지 않는다.
  - `bindings/<언어>/bug/` 디렉토리에 버그 리포트를 작성한다.
  - 리포트에는 최소한 다음을 포함한다.
    - 재현 조건 (소켓 타입, 패턴, 메시지 크기, transport 등)
    - 기대 동작
    - 실제 동작
    - 재현 코드 또는 테스트 참조
  - 바인딩 측에서 workaround가 필요하면 workaround임을 명시하고 bug 리포트를
    참조한다.

## Test Matrix
- 이 섹션은 각 바인딩이 최소한 가져야 할 테스트 항목을 정리한다.
- 바인딩별 표면은 달라도 아래 의미 계약은 모두 검증해야 한다.
- `Surface Tests`, `Contract Tests`, `Behavior Tests`, `Failure Contract Tests`,
  `Helper/Facade Tests`, `Optimization Guard Tests`, `Boundary Validation Tests`,
  `Option Tests`, `Ownership Tests`는 모든 바인딩의 기본 `Required` 항목이다.
- `Callback Tests`, `Monitor Tests`, `Poller Tests`, `Service Tests`, `Codec Tests`,
  `Sample Smoke Tests`는 해당 public API, extension package, sample suite를 제공하는
  바인딩에서 `Conditional` 항목이다.
- `Language Runtime Tests`는 런타임 특성 때문에 위험이 생기는 바인딩에서
  `Language-specific` 항목이다.

### Required: Surface 테스트
- canonical public API surface test
- socket type 역할 분리 확인
- typed option surface 존재 확인
- socket 공통 TLS helper 존재 확인
- service TLS helper 존재 확인
- raw option bag 비노출 확인
- monitor canonical surface 존재 확인
  - `recv()`

### Required: Contract 테스트
- FFI/native 호출 매핑 검증
  - 바인딩 public API 호출이 올바른 C API 함수에 매핑되는지 확인
  - 파라미터 전달과 반환값 변환이 올바른지 확인
- managed ↔ native 경계 타입 변환 검증
  - 언어 타입에서 C 타입으로의 변환이 올바른지 확인
  - C 타입에서 언어 타입으로의 변환이 올바른지 확인
- 리소스 lifecycle 검증
  - context/socket native handle 생성과 해제가 누수 없이 동작하는지 확인
  - 예외/오류 경로에서도 native 리소스가 정리되는지 확인

### Required: Behavior 테스트
- 바인딩 레이어가 core 계약을 올바르게 중계하는지 검증한다.
- 목적은 core 메시징 기능 재검증이 아니라 바인딩 경로의 정확성 확인이다.
- blocking 경로:
  - `send` → core send 중계 성공
  - `recv` → core recv 중계 성공
  - `publish` → core publish 중계 성공
  - `subscribe` → core subscribe 중계 성공
  - routed `send` → routing id 포함 중계 성공
- non-blocking 경로:
  - `recv` non-blocking → 데이터 없음 시 empty 반환
  - `subscribe` non-blocking → 데이터 없음 시 empty 반환
  - `receiveSubscriptionEvent` non-blocking → 데이터 없음 시 empty 반환
  - `send` 실패 시 예외 또는 오류 경로 확인
  - `publish` 실패 시 예외 또는 오류 경로 확인

### Required: Helper/Facade 테스트
- public helper와 facade가 단순 native 호출 이상의 의미를 제공하는 경우 그 의미를
  직접 검증한다.
- `Message`, `Received`, multipart collection, routing id value/codec, typed option
  facade, domain object, request/reply helper, topology snapshot value object 같은
  바인딩 제공 타입의 불변식을 검증한다.
- helper가 native 세부사항을 사용자에게 누출하지 않는지 확인한다.
- helper가 성공/실패, empty payload, one empty message, multipart boundary를
  구분해서 유지하는지 확인한다.
- helper가 public API에 없는 internal sequencing을 사용자에게 요구하지 않는지
  확인한다.
- convenience API가 canonical API와 다른 의미를 만들지 않는지 확인한다.

### Required: Optimization Guard 테스트
- hot path가 High-Performance Binding Policy를 계속 지키는지 검증한다.
- send/recv/request/reply/publish/subscribe 내부 경로가 `*_part` substrate를
  사용하는지 확인한다.
- aggregate native 함수 호출, 숨은 double materialization, 불필요한 eager copy,
  반복 호출마다 생기는 closure/boxing/allocation이 다시 들어오지 않았는지 확인한다.
- callback, dispatch, poller, request completion 경로에서 숨은 blocking wait,
  sleep, busy wait, thread join이 생기지 않았는지 확인한다.
- 이 검증은 항상 micro benchmark일 필요는 없다. 안정적으로 자동화할 수 있으면
  source-level/static check, public API allocation check, stress smoke, perf gate 중
  가장 낮은 비용의 방식을 사용한다.
- perf benchmark는 수치 회귀를 담당하고, optimization guard test는 금지된 구조가
  코드에 들어오지 않도록 막는 역할을 담당한다.

### Required: Failure Contract 테스트
- blocking `send` failure가 예외 또는 언어별 오류 경로로 caller에 전달되는지 확인
- blocking `publish` failure가 예외 또는 언어별 오류 경로로 caller에 전달되는지 확인
- `send` backpressure 예외 확인
- `send` not-ready 예외 확인
- `publish` backpressure 또는 not-ready 예외 확인
- native `NO_DATA` 외 오류가 무시되지 않는지 확인
- callback mode와 direct recv 충돌 시 native 계약대로 오류가 전달되는지 확인
- direct recv 불가 상태에서 empty/null로 숨기지 않는지 확인
- native `NO_DATA`만 empty/non-success 결과로 처리되는지 확인

### Required: Boundary Validation 테스트
- `RoutingId` 최대 길이 경계 (255바이트 OK)
- `RoutingId` 초과 길이 즉시 오류 반환 (256바이트 이상 → 예외)
- `Duration -> int millis` overflow 경계
- offset/length bounds 검증
- null 불가 인자 검증
- enum 범위 밖 값 검증
- `channel_name` 255바이트 초과 즉시 오류 반환 (고정 크기 `char[256]`)
- `endpoint` 255바이트 초과 즉시 오류 반환 (고정 크기 `char[256]`)
- topic/filter에 embedded null 문자 포함 시 즉시 오류 반환

### Required: Option 테스트
- common option typed getter/setter
- socket type별 typed option getter/setter
- 잘못된 소켓 타입에서 option 역할 접근 차단
- raw integer 대신 enum/boolean surface가 제공되는지 확인

### Required: Ownership 테스트
- send 성공 시 ownership 이동 계약 (native에 넘어감, 바인딩이 이후 접근 금지)
- send 실패 시 restore 또는 caller ownership 유지 계약
- 생성 후 send하지 않은 메시지의 명시적 close/해제 (close 없으면 native 메모리 누수)
- recv 결과 ownership 계약 (바인딩이 받아서 해제 책임)
- callback 후 frame validity 계약
- multipart receive shape와 callback delivery shape 일치 여부

### Conditional: Callback 테스트
- public callback API가 있는 경우 callback delivery를 검증한다.
- callback이 받은 message 또는 multipart payload의 ownership을 검증한다.
- callback 예외, panic, rejected promise, delegate exception 같은 언어별 실패가
  문서화된 오류 경로로 전달되는지 확인한다.
- callback delegate/function/object lifetime이 native callback보다 짧아져
  use-after-free를 만들지 않는지 검증한다.
- callback 안에서 금지된 blocking wait나 hidden thread join이 발생하지 않는지
  확인한다.

### Conditional: Monitor 테스트
- blocking monitor `recv` 성공 경로
- non-blocking monitor recv empty path
- monitor callback/state 변화와 data plane readiness 일치 여부

### Conditional: Poller 테스트
- raw socket readiness 또는 fd readiness가 public poller API로 전달되는지 확인한다.
- poller가 지원하지 않는 service-specific handle을 조용히 받아들이지 않는지 확인한다.
- readiness event 값은 data plane contract를 대체하지 않는다는 점을 검증한다.

### Conditional: Service 테스트
- spot/actor public API를 제공하는 바인딩은 해당 service lifecycle을 최소 경로로
  검증한다.
- close/connect/unbind 같은 lifecycle 제약이 public API에서 native 계약대로
  전달되는지 확인한다.
- spot publish/subscribe, spot request/reply, SPOT status/snapshot은 public
  surface가 있으면 roundtrip 또는 snapshot 검증을 수행한다.
- service test는 service layer 바인딩 계약 검증이 목적이다. core service 전체
  matrix를 모든 언어에서 다시 실행하지 않는다.

### Conditional: Codec 테스트
- codec extension package를 제공하는 바인딩은 codec별 payload roundtrip을 검증한다.
- core binding package가 codec dependency를 필수로 끌어들이지 않는지 확인한다.
- serializer 선택 규칙이 있는 언어는 기본 serializer와 오류 경로를 검증한다.

### Conditional: Sample Smoke 테스트
- sample suite를 제공하는 바인딩은 canonical sample set의 실행 smoke를 제공한다.
- sample smoke는 public API 사용 가능성을 확인하는 최소 검증이다.
- sample smoke는 core transport matrix, stress, perf 측정을 대신하지 않는다.

### Language-specific: Runtime 테스트
- .NET: `IDisposable`, `SafeHandle`, delegate lifetime, `GCHandle`, native library
  loader, `ZlinkException` mapping을 검증한다.
- Java: `AutoCloseable`, JNI object lifetime, checked/unchecked exception policy,
  classloader/native loader 경계를 검증한다.
- Go: cgo pointer rule, finalizer에 의존하지 않는 explicit close, `(T, error)`
  mapping을 검증한다.
- Rust: ownership move, borrow lifetime, `Drop`, `Send`/`Sync` 노출 여부, concrete
  error type mapping을 검증한다.
- Python: buffer protocol, reference counting, context manager, exception mapping을
  검증한다.
- Node: native addon lifetime, `Buffer` ownership, async callback error path,
  package export boundary를 검증한다.
- C++: RAII, move-only message ownership, exception type, installed header boundary를
  검증한다.
- C: raw ABI, errno/result code, caller-provided message lifecycle을 검증한다.

### 참고: Performance and Sample Verification
- 성능 회귀 검증은 Perf Policy (`doc/perf/`)가 담당한다. Test Matrix에 중복하지
  않는다.
- sample/helper의 canonical API 준수, send 실패 무시 방지, legacy surface
  우회 방지는 Review Checklist에서 검증한다. 자동화 테스트 항목이 아니다.

## 샘플 정책
- 샘플 제작 규칙은 [`doc/spec/sample/SAMPLE_POLICY.md`](https://kairos-code-dev.github.io/zlink/en/spec/sample/SAMPLE_POLICY/)
  를 단일 기준 문서로 사용한다.
- 이 문서는 `core/samples/`와 `bindings/*/samples/`를 함께 포괄한다.
- 바인딩 샘플을 추가, 수정, 리뷰할 때는 위 문서를 기준으로 판단한다.

## Perf 정책

perf 코드는 데모가 아니라 바인딩 라이브러리의 성능을 측정하고 개선하기 위한
코드다. perf 의 1차 목적은 바인딩 레이어의 비용을 드러내고, 병목과 회귀를
식별하고, 개선 작업의 전후 차이를 측정하는 것이다.

**perf 정책의 단일 기준은 `doc/perf/` 정책 문서다.** CLI 옵션, 기본값, 출력
포맷, RESULT line 형식, 패턴/transport matrix, phase 규칙, 결과 저장, 실패
처리, 환경 변수 등 모든 세부 규격은 아래 문서를 따른다. 본 섹션에서 중복
정의하지 않는다.

- [`doc/perf/PERF_POLICY.md`](../../../doc/perf/PERF_POLICY.md) — 공통 perf 정책
  (공통 원칙, 디렉터리 구조, RESULT 형식, 결과 저장, 출력 형식, 실패 처리,
  환경 변수, 리팩토링 원칙, 언어별 적용 범위)
- [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](../../../doc/perf/PERF_SINGLE_TEST_POLICY.md) — single suite 정책
- [`doc/perf/PERF_MULTI_TEST_POLICY.md`](../../../doc/perf/PERF_MULTI_TEST_POLICY.md) — multi suite 정책

### 바인딩 perf 원칙

- perf 코드는 `doc/perf` 정책을 준수한다.
- `core/perf` 에서 제공하는 패턴과 시나리오를 기준으로 한다.
- core perf와 비교 가능한 시나리오를 유지하면서, 각 언어 스타일에 맞게 작성한다.
- 측정 anchor point, phase 의미, metric 집합, RESULT line 의미를 바꾸지 않는다.
- perf 정책은 성능 측정 surface를 공식 제공하는 바인딩에서는 `Required`다.
  perf 코드를 아직 제공하지 않는 바인딩에는 `Target`으로 본다.

### 바인딩 API Spec 문서

각 바인딩의 API surface는 아래 문서를 참조한다.
perf 정책은 [`doc/perf/PERF_POLICY.md`](../../../doc/perf/PERF_POLICY.md)에서 전 언어 공통으로 관리한다.

| 바인딩 | API Spec |
|--------|----------|
| C | [`c/README.md`](c/README.en.md) |
| C++ | [`cpp/README.md`](cpp/README.en.md) |
| Java | [`java/README.md`](java/README.en.md) |
| .NET | [`dotnet/README.md`](dotnet/README.en.md) |
| Node.js | [`node/README.md`](node/README.en.md) |
| Python | [`python/README.md`](python/README.en.md) |
| Go | [`go/README.md`](go/README.en.md) |
| Rust | [`rust/README.md`](rust/README.en.md) |

### Perf 리뷰 체크리스트

- 이 perf 가 바인딩 라이브러리 비용을 측정하고 있는가
- 핵심 send/recv/callback 경로가 perf 파일 본문에서 직접 읽히는가
- 각 패턴이 별도 파일로 분리되어 있는가
- `core/perf` 패턴과 정렬되어 있는가
- `doc/perf` 정책을 준수하는가

## 스크립트 위치 정책
- 실행 스크립트는 실행 대상과 같은 디렉토리에 위치한다.
- 바인딩 루트가 아니라 각 하위 디렉토리에 둔다.

| 용도 | 위치 | 스크립트 예시 |
|------|------|---------------|
| 테스트 | `bindings/<언어>/tests/` | `run_tests.sh` |
| 샘플 | `bindings/<언어>/samples/` | `run_samples.sh` |
| perf | `bindings/<언어>/perf/` | `run_benchmarks.sh`, `run_benchmarks_multi.sh` |

- Windows 지원이 필요한 경우 `.ps1` 도 함께 제공한다.
- 바인딩 루트(`bindings/<언어>/`)에 `run_samples.sh` 같은 wrapper를
  두지 않는다. 이 위치의 wrapper는 `samples/run_samples.sh`와 중복되고,
  어느 것이 정답인지 혼선을 만든다.
- CI나 전체 검증을 위해 테스트+샘플+perf를 한번에 실행하는 orchestration
  스크립트가 필요하면 `bindings/<언어>/run_all.sh` 같은 이름으로 둘 수 있다.
  이 스크립트는 개별 `tests/run_tests.sh`, `samples/run_samples.sh` 등을
  호출하는 진입점이며, 개별 스크립트를 대체하지 않는다.

## 리뷰 체크리스트
- public API가 multipart-only인가
- blocking/non-blocking이 별도 이름으로 분리되지 않았는가
- 언어별 `Flags Policy`에 없는 public flag type 또는 중복 flag 경로가
  남아 있지 않은가
- raw option bag이 public에 남아 있지 않은가
- option 값이 enum/boolean/value object로 승격되었는가
- 타입별 역할이 제대로 닫혀 있는가
- blocking send 실패가 예외 또는 오류 경로로 반드시 caller에 전달되는가
- `send` 실패가 backpressure/not-ready를 포함해 모든 오류를 예외로 전달하는가
- binding이 truncation/overflow를 선검증하는가
- native 상태 오류를 바인딩이 임의로 추론하지 않는가
- public surface test와 behavior test가 같이 있는가
- 값 객체 검증과 호출 직전 검증의 책임 위치가 설명 가능한가
- 언어별 `Flags Policy`에 없는 legacy flag 타입이 public contract에서 제거되었는가
- sample code가 canonical API만 사용하는가
- helper가 blocking send 실패를 무시하지 않는가
- helper가 deprecated/legacy surface를 우회 호출하지 않는가

## POSD 기반 구현 완성 정책
- 이 섹션은 바인딩 구현을 완성하고 리팩터링할 때 적용하는 POSD 기반 절차를
  정의한다.
- 바인딩은 기능 나열이 아니라 구조적 정확성을 기준으로 완성한다.
- 완성 기준은 Socket Capability Matrix, Callback API Policy, Option Policy,
  Test Matrix, Sample Policy다.
- 리팩터링은 코드를 이동하는 것이 아니라 시스템 복잡도를 줄이는 것이다.

### 완성 순서
- 바인딩 구현은 아래 순서를 따른다.
- 각 단계는 이전 단계의 결과에 의존한다.
- 한 단계를 건너뛰고 다음 단계를 진행하지 않는다.

#### 1단계: Capability Matrix 정렬
- Socket Capability Matrix를 기준으로 각 소켓 타입의 public API를 검토한다.
- 있어야 하는데 없는 API를 추가한다.
- 있으면 안 되는데 노출된 API를 제거하거나 internal로 이동한다.
- 검증: surface test가 matrix와 일치해야 한다.
- 대표 위반 예:
  - StreamSocket에 `connect()` 노출 → 제거
  - StreamSocket에 `disconnectRid()` 노출 → 제거
  - StreamSocket에 `detachStream()` 노출 → 제거
  - Node에 `setSendReadyHandler` 없음 → 추가
  - 잘못된 소켓에 publish/subscribe 노출 → 제거

#### 2단계: 이름 정규화
- Naming Policy와 Callback API Policy 기준으로 canonical 이름을 맞춘다.
- 이름만 다르고 의미가 같은 API는 canonical 이름으로 통일한다.
- deprecated alias는 제거한다.
- 검증: surface test에서 canonical 이름 존재를 확인한다.
- 대표 위반 예:
  - public `recvHandler` / `onReceive` → 제거하거나 internal raw STREAM bridge로 이동
  - `spotDispatchHandler` → `setDispatchHandler`
  - `on_topic_message` → `subscribe`

#### 3단계: 깊은 모듈 구조
- POSD deep module 원칙에 따라 public 타입의 깊이를 확보한다.
- 각 public 타입이 단순 pass-through가 아니라 내부에서 검증, ownership,
  shape 규칙을 캡슐화하는지 확인한다.
- 얕은 래퍼 판별 기준:
  - native 함수를 1:1로 감싸기만 하고 새 의미를 추가하지 않는가
  - 호출자가 native 계약(시퀀스, 크기, 인코딩)을 알아야 사용할 수 있는가
  - 동일 규칙이 여러 소켓 타입에 중복 구현되어 있는가
- 얕은 래퍼를 발견하면:
  - 검증을 값 객체 또는 facade 내부로 이동한다
  - 중복 규칙을 한 모듈에 모은다
  - pass-through만 하는 public 타입은 제거하거나 internal에 병합한다
- 대표 위반 예:
  - RoutingId 길이 검증이 각 소켓 타입마다 중복 → RoutingId 값 객체 하나로 모은다
  - monitor event가 raw int → typed event surface로 승격한다
  - option value가 raw int → enum/boolean/Duration으로 승격한다

#### 4단계: 변경 파급 제거
- 같은 규칙이 여러 곳에 흩어진 지점을 찾아서 한 모듈에 모은다.
- 판별 기준:
  - 정책 하나가 바뀌면 2개 이상의 파일을 고쳐야 하는가
  - 새 소켓 타입을 추가할 때 기존 코드를 N곳 수정해야 하는가
- 대표 위반 예:
  - send failure contract 규칙이 소켓 타입마다 별도 구현
  - blocking/non-blocking 분기가 소켓 타입마다 별도 구현
  - option validation이 각 option setter마다 별도 구현

#### 5단계: 정보 은닉 강화
- public API가 native 세부사항을 노출하는 지점을 찾아서 facade 뒤로 숨긴다.
- 판별 기준:
  - 사용자가 errno, flag 상수, native struct 크기를 알아야 하는가
  - 사용자가 internal sequencing(호출 순서)을 기억해야 하는가
  - public API에 native handle, raw pointer, raw buffer가 노출되는가
- 대표 위반 예:
  - raw `setSockOptRaw` / `setOption(int, byte[])` 가 public
  - monitor event에 raw int mask가 그대로 노출
  - 언어별 `Flags Policy`에 없는 legacy flag 타입이 public 타입으로 남아 있음

#### 6단계: 테스트 Matrix 완성
- Test Matrix의 `Required` 카테고리는 모든 바인딩에서 작성하거나 보강한다.
- 해당 public API, extension package, sample suite를 제공하는 바인딩은 관련
  `Conditional` 카테고리도 작성하거나 보강한다.
- 언어 런타임의 수명, 예외, native loader 위험이 있는 바인딩은 관련
  `Language-specific` 카테고리를 작성하거나 보강한다.
- 완성 기준:
  - Surface test가 Socket Capability Matrix를 검증한다
  - Contract test가 FFI 매핑과 lifecycle을 검증한다
  - Behavior test가 blocking/non-blocking 경로를 검증한다
  - Helper/Facade test가 바인딩 제공 helper의 의미 계약을 검증한다
  - Optimization Guard test가 hot path 최적화 불변식을 검증한다
  - Failure Contract test가 send/receive 오류 계약을 검증한다
  - Boundary test가 값 경계를 검증한다
  - Option test가 typed surface를 검증한다
  - Ownership test가 send/recv ownership을 검증한다
  - 해당 public API가 있으면 Callback, Monitor, Poller, Service, Codec test가
    public contract를 검증한다
  - sample suite가 있으면 Sample Smoke test가 canonical API 실행을 검증한다

#### 7단계: 샘플 정렬
- Canonical Sample Set 기준으로 샘플을 완성한다.
- 각 샘플이 canonical API만 사용하는지 확인한다.
- 1-5단계에서 이름이나 API가 바뀌었다면 샘플도 같이 갱신한다.

### 리팩터링 판단 기준
- 다음 질문에 "예"이면 리팩터링이 필요한 지점이다.
  - 이 public 타입을 제거하면 사용자가 잃는 것이 없는가 → 얕은 래퍼
  - 이 규칙을 고치면 3개 이상의 파일을 건드려야 하는가 → 변경 파급
  - 사용자가 이 API를 쓰려면 다른 API의 내부 동작을 알아야 하는가 → 정보 누출
  - 같은 능력이 2개 이상의 이름으로 노출되는가 → 중복 surface
  - 사용자가 호출 순서를 기억해야 올바르게 동작하는가 → 시간 순서 의존

### 리팩터링 종료 조건
- 리팩터링은 아래 조건이 모두 충족될 때까지 반복한다.
- 하나라도 남아 있으면 완료가 아니다.
- 판단은 POSD 관점에서 수행한다.
- 종료 조건의 범위는 해당 바인딩이 구현하기로 한 scope에 한정한다.
  - `Required` 항목: 모든 바인딩에 적용
  - `Conditional` 항목: 해당 public API, extension package, sample suite를
    제공하는 바인딩에 적용
  - `Language-specific` 항목: 해당 런타임 위험이 있는 바인딩에 적용
  - `Recommended` 항목(예: 샘플): 공개 배포 바인딩에 적용

1. **Capability Matrix 완전 정렬**
   - Socket Capability Matrix의 모든 `Y` 항목이 public API에 존재한다.
   - Socket Capability Matrix의 모든 `—` 항목이 public API에 노출되지 않는다.
   - 해당 바인딩이 구현하는 서비스 계층 컴포넌트의 Capability Matrix도
     동일하게 정렬한다.
     바인딩이 구현하지 않으면 종료 조건에서 제외한다.
   - Surface test가 이를 검증하고 통과한다.

2. **이름 정규화 완료**
   - 모든 public API가 Naming Policy의 canonical 이름을 사용한다.
   - deprecated alias가 남아 있지 않다.
   - Callback API Policy의 canonical 이름(`setPacketHandler`,
     `setDispatchHandler`, `setSendReadyHandler`)이
     해당 역할에 맞게 존재한다.

3. **얕은 래퍼 제거**
   - native 함수를 1:1로 감싸기만 하는 public 타입이 없다.
   - 모든 public 타입이 검증, ownership, shape 규칙 중 하나 이상을 캡슐화한다.
   - `RecvPart`, `RecvRoutedPart`, `SubscribePart` 또는 언어별 동등 이름이
     public API에 없다. part 단위 수신은 runtime/internal substrate로만 존재한다.
   - `requestFrame(...)`처럼 protocol envelope을 그대로 드러내는 helper가 public
     표면에 없다.
   - `dealer.reply(requestToken, parts)`처럼 DEALER의 송신 능력과 맞지 않는 reply
     helper가 public 표면에 없다.

4. **변경 파급 해소**
   - 동일 규칙이 2개 이상의 모듈에 중복 구현되어 있지 않다.
   - 정책 변경 시 수정해야 할 파일이 1개다.

5. **정보 은닉 확보**
   - public API에 raw option bag, 정책 밖 legacy/raw flag, raw native struct,
     raw errno가 노출되지 않는다.
   - 사용자가 internal sequencing을 알지 않아도 API를 올바르게 사용할 수 있다.

6. **Test Matrix 완성**
   - 모든 `Required` 테스트가 존재하고 통과한다.
   - 해당 바인딩의 scope에 포함되는 `Conditional` 테스트가 존재하고 통과한다.
   - 해당 런타임 위험에 필요한 `Language-specific` 테스트가 존재하고 통과한다.

7. **Sample 정렬 완료**
   - Canonical Sample Set의 모든 샘플이 존재한다.
   - 해당 바인딩이 구현하는 서비스 계층 샘플도 포함한다.
   - 구현하지 않는 `Target` 컴포넌트의 샘플은 제외한다.
   - 모든 샘플이 canonical API만 사용한다.
   - deprecated/legacy 경로를 사용하는 샘플이 없다.

8. **Dead code 제거 완료**
   - 리팩터링 과정에서 발생한 모든 불필요한 코드가 제거되었다.
   - deprecated alias, legacy wrapper, 사용되지 않는 import/using/require가
     남아 있지 않다.
   - Capability Matrix에서 `—`로 표시된 API의 구현 코드가 internal에도 불필요하게
     남아 있지 않다.
   - 이름 정규화로 교체된 옛 이름의 함수/메서드/타입이 남아 있지 않다.
   - 호출되지 않는 private/internal helper가 남아 있지 않다.
   - 참조되지 않는 상수, enum 값, 타입 alias가 남아 있지 않다.
   - 주석으로 처리된 코드 블록(`// removed`, `// deprecated`, `// remove later`)이
     남아 있지 않다.
   - 빈 파일, 빈 클래스, 빈 모듈이 남아 있지 않다.
   - dead code는 "나중에 쓸 수 있으니까" 남겨 두지 않는다. 필요하면 git
     history에서 복원한다.

### 리팩터링 반복 규칙
- 1-7단계를 한 번 수행한 뒤, 종료 조건을 다시 점검한다.
- 앞 단계의 변경이 뒤 단계에 영향을 줄 수 있으므로, 종료 조건이 하나라도
  미충족이면 해당 단계부터 다시 수행한다.
- 종료 조건 8개가 모두 충족될 때까지 반복한다.
- "더 고칠 곳이 보이지 않는다"가 아니라 "종료 조건 8개가 모두 통과한다"가
  완료 기준이다.

### 리팩터링 금지 사항
- 구조 개선을 이유로 의미 계약을 바꾸면 안 된다.
- 내부 리팩터링으로 public API의 시그니처가 달라지면 안 된다.
  - 시그니처가 달라져야 하면 그것은 API 변경이지 리팩터링이 아니다.
- 성능 개선을 이유로 correctness를 타협하면 안 된다.
- "나중에 쓸 수 있으니까" 미리 추상화를 만들면 안 된다.
- 한 번만 쓰이는 코드를 utility/helper로 빼면 안 된다.

## 구현 리뷰 체크리스트
- 이 섹션은 public API 정책을 구현에 반영했는지 확인하는 리뷰 체크리스트다.
- 아래 항목은 새로운 public API 제안이 아니다. 이미 정의된 계약과 경계 규칙을
  구현, 테스트, 샘플이 지키는지 확인하는 기준이다.
- 항목은 바인딩별 리뷰와 리팩터링 작업의 기본 체크리스트로 사용한다.

### 공개 vs 내부 경계 후속 작업

- Java:
  - public package에 남아 있는 internal 성격 타입(`SocketCore`,
    `MessagePlane`, request/reply support helper 등)을 internal package 또는
    implementation package로 이동해야 한다.
  - JPMS를 사용한다면 documented public package만 export 하도록 정리해야 한다.
- .NET:
  - `InternalsVisibleTo`는 test 지원 범위로만 제한해야 한다.
  - perf 프로젝트가 internal surface에 접근하지 않도록 assembly visibility를
    다시 닫아야 한다.
- C:
  - helper substrate와 public C binding header가 실제로 분리되면,
    `core/include/zlink.h` 중심 설명을 public C binding header 기준으로 다시
    정리해야 한다.
  - 설치되는 public header와 private substrate header의 경계를 문서와 패키징에
    함께 반영해야 한다.

### 값 검증 후속 작업
- `RoutingId`
  - 값 객체 생성 시 길이 상한 검증
  - raw 경로가 남아 있다면 native 호출 직전 재검증
- `Duration` 기반 옵션
  - `int millis` 변환 overflow 검증
  - 음수 허용/비허용 계약 명시
- topic/filter/string identifier
  - 고정 크기 output buffer 경로의 재할당 정책 점검
  - truncation 없이 전체 문자열을 처리하는지 점검
- offset/length 기반 byte API
  - bounds 검증 일관화
- enum wrapper가 없는 raw 정수 옵션
  - enum 또는 boolean 승격 후보 조사

### 공개 표면 후속 작업
- legacy flag 타입
  - 언어별 `Flags Policy`에 없는 public flag type 또는 중복 flag 경로 제거 여부 재확인
  - 필요한 경우 internal 이동 여부 결정
- monitor plane
  - `recv()` canonical surface 유지 여부 확인
- callback API
  - callback payload shape가 direct receive shape와 동일한지 재확인
- 단일 메시지 편의 메서드
  - public receive/subscribe 편의 오버로드 잔존 여부 점검

### 옵션 표면 후속 작업
- raw option bag 잔존 여부 조사
- socket type별 option 역할 누수 여부 조사
- option value가 아직 `int`에 머무는 항목 목록화
- context option도 같은 기준으로 typed facade 적용 여부 검토

### 오류 계약 후속 작업
- binding validation 예외와 native 예외가 혼재된 경로 조사
- 바인딩이 errno를 임의로 해석하는 경로 조사
- native `NO_DATA` 외 오류를 잘못 empty/bool 경로로 숨기는 코드 조사
- blocking send 실패를 무시하는 helper/sample 조사

### 성능 후속 작업
- hot path send/recv 경로의 숨은 복사 조사
- `Message`, `Received`, `TopicMessage` 생성 과정의 불필요한 컬렉션/배열
  할당 조사
- callback path와 direct path 비용 차이 조사
- string/topic/routing-id 변환의 인코딩/디코딩 비용 조사
- sample과 helper가 느린 대체 경로를 기본 사용법처럼 노출하는지 조사

### POSD 후속 작업
- 얕은 래퍼만 제공하는 public 타입 조사
- 한 규칙이 여러 모듈에 흩어진 변경 파급 지점 조사
- 사용자가 internal sequencing을 알아야 하는 temporal API 조사
- facade 뒤로 숨길 수 있는 raw/native 개념 누수 지점 조사

### 소유권과 콜백 후속 작업
- send failure restore 경로와 consume 경로가 문서와 일치하는지 점검
- callback 후 frame validity 계약 재검증
- callback mode와 direct recv 충돌 시 native 계약대로 오류가 전달되는지 점검

### 테스트 후속 작업
- public surface 변경마다 public surface test 존재 여부 확인
- value boundary 검증 테스트 추가
  - 예: `RoutingId` 최대 길이
  - 예: `Duration` overflow
- option negative 역할 테스트 보강
- ownership/callback regression 유지 여부 확인

## 바인딩 요구사항

| Binding | 언어 버전 | 런타임/프레임워크 | 빌드 툴 |
|---------|-----------|-------------------|---------|
| C++ | C++20 | — | CMake 3.10+ |
| .NET | C# 12 | .NET 8.0 | MSBuild |
| Java | Java 22 | JDK 22 | Gradle 8.10.2 |
| Go | Go 1.22+ | — | Go modules |
| Rust | Rust 2024 edition | MSRV 1.85+ | Cargo |
| Node | TypeScript 5.8 | Node 22+ | npm |
| Python | Python 3.9 | CPython 3.9+ | setuptools 68+ |
- 각 바인딩의 정확한 버전은 해당 프로젝트 설정 파일이 기준이다.
  - C++: `CMakeLists.txt`
  - .NET: `Zlink.csproj` (`PackageId` / `RootNamespace`: `Systems.Zlink`)
  - Java: `build.gradle`, `gradle-wrapper.properties`
  - Go: `go.mod`
  - Node: `package.json`, `tsconfig.json`
  - Python: `pyproject.toml`

## API 레퍼런스

각 바인딩은 해당 언어의 표준 문서 도구로 API 레퍼런스를 생성한다.

| Binding | 문서 도구 | 생성 명령 | 출력 위치 |
|---------|-----------|-----------|-----------|
| C++ | Doxygen | `doxygen Doxyfile` | `cpp/doxygen/html/` |
| Java | Javadoc (Gradle) | `./gradlew javadoc` | `java/build/docs/javadoc/` |
| Python | Sphinx + autodoc | `sphinx-build -b html docs docs/_build/html` | `python/docs/_build/html/` |
| Node | TypeDoc | `npx typedoc` | `node/typedoc/html/` |
| .NET | DocFX | `docfx docfx.json` | `dotnet/_site/` |
| Go | godoc / pkgsite | `go doc ./...` | (동적 서버) |
| Rust | rustdoc | `cargo doc --no-deps` | `rust/target/doc/zlink/` |

- 생성 명령은 각 바인딩 디렉터리에서 실행한다.
- 출력 디렉터리는 `.gitignore`로 추적에서 제외한다.
- 각 바인딩의 `README.*.md` 파일에 상세 생성 절차와 스코프가 명시되어 있다.

## Routing ID로 Peer 끊기

- 모든 바인딩은 connectable raw socket 타입에 대해 core의 peer-rid disconnect 표면을 노출한다.
- raw socket API는 `zlink_disconnect_rid()`에, SpotNode API는 `zlink_spot_node_disconnect_peer_rid()`에 매핑한다.
- `StreamSocket`은 bind-only이며 peer-rid disconnect를 노출하지 않는다.
- Spot facade 타입도 별도의 peer-rid disconnect 메서드를 노출하지 않는다. peer mesh 소유권은 SpotNode에 있기 때문이다.

| Language | Raw socket name | SpotNode name |
|---|---|---|
| C | `zlink_disconnect_rid` | `zlink_spot_node_disconnect_peer_rid` |
| C++ | `disconnect_rid` | `disconnect_peer_rid` |
| Python | `disconnect_rid` | `disconnect_peer_rid` |
| Node | `disconnectRid` | `disconnectPeerRid` |
| Go | `DisconnectRID` | `DisconnectPeerRID` |
| Rust | `disconnect_rid` | `disconnect_peer_rid` |
| Java | `disconnectRid` | `disconnectPeerRid` |
| .NET | `DisconnectRid` | `DisconnectPeerRid` |

바인딩은 `ZLINK_OPT_RID_DUPLICATE_POLICY`, `ZLINK_RID_DUPLICATE_REJECT`,
`ZLINK_RID_DUPLICATE_HANDOVER`, 그리고 connect 결과 값 `NOT_FOUND`, `CONFLICT`,
`BUSY` 를 각 언어의 일반적인 enum/오류 매핑 스타일로 노출해야 한다.

- C 바인딩은 native socket option contract를 통해 `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` 값 `0x3034` 를, context option contract를 통해 `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES` 값 `18` 을 노출한다.
- 상위 바인딩은 이 기능을 typed context option facade로 노출해야 한다.
- socket, SpotNode, Spot 의 public facade는 메시지 단위 옵션을 추가하지 않는다.
- 호환성을 위해 raw socket 경로를 남겨 둔다면 canonical API와 명확히 분리하고, 새 문서·샘플·테스트에서는 사용하지 않으며, C 계약(`int` bytes, raw 기본값 `0`, 음수 값은 `EINVAL` 로 실패)을 그대로 유지해야 한다.

## 관련 문서
- `bindings/cpp/`
- `bindings/dotnet/`
- `bindings/java/`
- `bindings/go/`
- `bindings/rust/`
- `bindings/node/`
- `bindings/python/`

## Core API Surface 6.0.0 정렬

- Actor create와 join payload는 aggregate multipart payload를 사용한다.
- 공개 바인딩 API는 remote actor create, actor join, actor join receive, actor join reply에 대해 메시지 컬렉션을 받는다.
- 단일 메시지 편의 경로는 해당 언어 README가 그 편의 표면을 명시적으로 유지하기로 한 경우에만 허용한다. 그렇지 않으면 breaking alignment 과정에서 canonical multipart 경로 쪽으로 정리하면서 제거한다.
- 유지하는 경우에도 내부적으로는 multipart 경로를 호출해야 하며, empty payload와 비어 있는 메시지 하나가 계속 구분될 수 있어야 한다.
- admission handler는 callback 동안만 유효한 borrowed payload view를 받는다.

Public Registry scalar 설정은 core 8.4.3에서 공개 Discovery/Registry C API와 함께
제거되었다. 바인딩은 registry option 표면, 이름 있는 registry setter,
compatibility alias를 현재 공개 API로 유지하면 안 된다.

## Spot Route Bridge API

- 바인딩은 `SpotNode`가 channel socket을 소유하지 않도록 `SpotRouteBridge` 또는 같은 의미의 typed handle을 노출해야 한다.
- bridge는 caller/channel runtime이 소유한 `ROUTER` socket을 참조하고, Spot route packet을 보내거나 channel receive loop에서 받은 SPOT relay packet을 SpotNode로 넘긴다.
- bridge를 닫아도 등록된 channel socket은 닫히지 않는다.

언어별 API는 다음 의미를 빠뜨리지 않아야 한다.

- `createRouteBridge(options)` 또는 동등한 생성자
- `attachRouterChannel(channelName, routerSocket)`
- `sendToSpot(targetNode, targetSpot, parts)`
- `requestToSpot(targetNode, targetSpot, parts, replyHandler, timeout)`
- `handleRouterReceived(channelName, received)`
- `close` 또는 `dispose`

`timeout == 0`은 bridge 기본 timeout을 사용한다. `handleRouterReceived`가 handled
결과를 반환하면 바인딩은 payload 소유권이 bridge로 넘어갔음을 호출자에게 분명히 표현해야
한다.

SpotNode에 router channel peer를 직접 붙이는 예전 C API는 공개 계약에 없다.
framework adapter는 그 경로를 새 구현에 사용하면 안 된다.
