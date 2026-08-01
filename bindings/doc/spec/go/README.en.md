[Spec Index](https://kairos-code-dev.github.io/zlink/en/spec/) · [Bindings Policy](../README.en.md)

# Go Binding Implementation Blueprint

This document defines the expected Go library shape. It is not an exhaustive
list of every exported identifier. The concrete public contract source is
`bindings/go/contracts/`.

A Go implementation is aligned when the `contracts` projection, implementation
owner files, tests, samples, perf runners, and runtime behavior follow this
blueprint and map the stable capabilities of `core/include/zlink.h` into
Go-idiomatic APIs.

This README describes the completed Go binding shape after it is aligned to the
shared policy in `../README.md`, and it is also the guide for Go refactoring
work. During the refactor, use this document to decide where each public
contract, implementation owner, cgo/native bridge helper, test, sample, and
perf import belongs. Once the Go binding is declared aligned, exported
identifiers, GoDoc, tests, samples, perf, and runtime behavior must match this
document.

The Go refactor is a breaking cleanup. Do not keep compatibility shims,
deprecated wrappers, duplicate construction paths, or old exported aliases only
to preserve the pre-refactor public surface.

This binding follows the shared bindings architecture map with Go naming:
the public `contracts` package is the consumer projection, while runtime
implementation stays under `internal/`.
Do not force a Java/.NET-style deep package tree when it would create public
import paths that Go users should not depend on.

Go is the public-package exception to the shared physical layout. The public
contract package remains flat because Go import paths are API, but its file
names still mirror the .NET categories: `core.go`, `messaging.go`,
`sockets.go`, `eventing.go`, service files, and `errors.go`. Private
implementation code under `internal/` should keep directory categories close to
the .NET runtime map, including `handles`, `buffers`, `options`, and `native`.

## Public Contract Source

- Public contract source: the public package under `bindings/go/contracts/`.
- Module projection: `bindings/go/go.mod`, the public
  `zlink.systems/zlink/contracts` package, and GoDoc for exported identifiers.
- Runtime implementation: private packages under `bindings/go/internal/`.
  Existing root implementation files are migration input, not the completed
  shape, because exported root-package names would become a second public
  surface.
- Native bridge: cgo bridge code, callback trampolines, request progress
  helpers, `bindings/go/include/`, and platform native artifacts under
  `bindings/go/native/`.
- Documentation role: this README defines shape and semantic coverage.
  Exported Go identifiers own the exact public member list. Each public
  identifier must still map to one of the shared contract categories.

Perf, samples, and external public-surface tests must import the public
`contracts` package and must not reach into implementation-only helpers or
native bridge details. Implementation tests may live beside private
implementation packages, but consumer tests must verify the public
`contracts` projection.

## Repository Layout

Use these paths consistently when changing the Go binding.

- Public contract: `bindings/go/contracts/`.
- Runtime implementation: `bindings/go/internal/` for private implementation
  packages. Existing root implementation files must either move there or stop
  exporting implementation-only names before the binding is declared aligned.
- Native bridge/artifacts: `bindings/go/internal/native/`,
  `bindings/go/native/`, and `bindings/go/include/`.
- Codec modules: not provided. Go bindings keep only raw `Message` and byte
  payload APIs.
- Tests: `bindings/go/tests/` and `bindings/go/*_test.go`.
- Samples: `bindings/go/samples/`.
- Perf: `bindings/go/perf/`.

Go import paths are part of the public API. The current public consumer
projection is the aggregate `zlink.systems/zlink/contracts` package. Do not
create a top-level `runtime/` package, because that would expose runtime
implementation as `zlink.systems/zlink/runtime`. Do not leave exported
implementation names in the module root as a parallel API. If the module root
is kept as an import path, it must be an intentional projection of the same
public contract, not the implementation owner.

The following tree is the aligned implementation structure. Exported types,
functions, errors, enums, and builder contracts belong in the public
`contracts` package. Do not create `contracts/core` or `contracts/sockets`
packages unless the public Go import policy is changed, because those paths
would become user-facing API. Private `internal/` packages own the cgo bridge
and runtime details. cgo declarations, raw pointers, native struct mirrors,
callback trampolines, request progress helpers, and marshalling must not become
the consumer-facing entrypoint.

File granularity follows the common policy in `../README.md`: keep one file
per independent public concept or tight operation/model group. Very small
callback, enum, error, or pass-through helper files should be merged into the
nearby contract file when that makes the public shape easier to read.

```text
bindings/go/
+-- go.mod
+-- doc.go
+-- contracts/
|   +-- core.go
|   +-- messaging.go
|   +-- sockets.go
|   +-- eventing.go
|   +-- service_spot.go
|   +-- errors.go
+-- internal/
|   +-- core/
|   +-- handles/
|   +-- messaging/
|   +-- buffers/
|   +-- sockets/
|   +-- eventing/
|   +-- service/
|   +-- options/
|   +-- errors/
|   +-- native/
+-- include/
+-- native/
+-- tests/
+-- samples/
+-- perf/
```

The public consumer projection is the `contracts` package. Samples, perf, and
public-surface tests must import that public package only. If an exported symbol
is added, reviewers must be able to point to its contract category owner. If
code only exists to call cgo or manage native lifetime, keep it unexported in
the implementation owner files under `internal/`.

## API Change Workflow

When mapping a new core capability:

1. Choose the shared contract category that owns the public behavior.
2. Add the exported type, method, or function to the public `contracts`
   package and keep its category ownership clear in source review.
3. Keep cgo calls and native state out of public signatures and consumer code.
4. Return `error` or typed errors in the normal Go style.
5. Avoid defining a provider-owned interface unless it removes real caller
   complexity.
6. Add public package tests and update samples/perf only through exported APIs.
7. Run `go vet` style checks where available and keep cgo pointer ownership
   explicit.

When refactoring existing code to this shape:

1. Move public behavior declarations to the `contracts` package.
2. Move cgo-backed runtime implementations to `internal/<category>/` packages.
3. Keep cgo declarations, native loading, and raw handles in private files or
   `internal/native/`.
4. Replace direct construction of implementation owners in user-facing code
   with public constructors or methods typed as contract concepts.
5. Remove compatibility exports that expose implementation helpers as public
   API.
6. Remove deprecated wrappers, duplicate operation-start names, and old naming
   aliases instead of preserving them as shims.
7. Update samples, perf, and public-surface tests to import only the public
   `contracts` package.

The refactor is complete only when Go-specific shortcuts below are removed.

- cgo handle owners, native bridge helpers, request progress helpers, and raw
  part-loop helpers are not exported.
- Samples, perf, and public-surface tests do not import implementation-only
  packages or use root-package shortcuts that bypass `contracts`.
- Public constructors and helper functions return contract-facing concrete
  types or narrow interfaces, not cgo/native implementation details.
- No public `runtime` package is introduced. Private implementation packages
  use `internal/`.

## Library Shape

The Go binding should follow normal Go conventions.

- Prefer concrete exported types for resources and values.
- Do not define large provider-owned interfaces for every resource type.
- Small interfaces may be defined by consumers or by the package only when
  they make call sites simpler.
- Methods return `(value, error)` or `error` where Go callers expect failure.
- No-data and temporary backpressure must be represented distinctly from hard
  errors.
- cgo handles, raw pointers, callback userdata, part-loop sequencing, and
  request pumps stay unexported.
- Use `Close()` for resource lifecycle. If a type starts background goroutines,
  its close semantics must be explicit and tested.

DTOs and values such as message, routing id, received metadata, topic message,
result values, snapshots, and option structs stay concrete.

## Contract / Runtime Placement Rules

- Exported public types, method contracts, enums, errors, and builder contracts
  belong in the public `contracts` package.
- Exported package functions, helper methods, and builder convenience helpers
  belong in `contracts` when callers can use them directly.
- cgo handle owners, request pumps, callback adapters, and part-loop helpers
  stay unexported under `internal/`.
- cgo declarations, raw pointers, C struct mirrors, marshalling helpers, and
  platform loading code stay in `internal/native` or private native helpers.
- The exported contract package must project the contract categories, not expose
  runtime packages as import paths.
- Public constructors may call private runtime implementations, but
  public signatures must not expose implementation-only types.

## Contract File Layout

Go keeps one public aggregate `contracts` package because import paths are
public API. Use source files inside that package to mirror the .NET
`Contracts/` category map without creating public subpackages.

- `core.go`: context, options, version/capability helpers, routing id, and
  package-level utility contracts.
- `messaging.go`: message, received metadata, topic messages, subscription
  events, and common payload helpers.
- `sockets.go`: socket families, typed options, callbacks, request/reply,
  publish/subscribe, stream packet APIs, and operation builders.
- `eventing.go`: monitor, poller, poll events, timer, and handler contracts.
- `service_spot.go`: service-layer contracts and domain models.
- `errors.go`: exported error values, error types, and result domains.

Small callback types, enum values, and result helpers may live in the nearby
contract file that gives them meaning. Avoid `types.go`, `models.go`,
`common.go`, or `utils.go` when the name hides the actual domain.

## Runtime File Layout

Runtime source mirrors the runtime classification in the
[.NET binding blueprint](../dotnet/README.en.md) but stays private.

- `internal/core`, `internal/messaging`, `internal/sockets`,
  `internal/eventing`, `internal/service`, and `internal/errors` own private
  facades over native runtime implementations.
- `internal/native` owns cgo declarations, native loading, raw handles,
  marshalling, callback trampolines, request progress helpers, option
  marshalling, and buffer conversion helpers.

Private runtime code may depend on public contract types. Public contract code
must not depend on private implementation details.

## Construction Entry Points

Go construction is exposed through public constructors and resource methods.

- `NewContext(...)` creates the runtime context implementation.
- `Context.PairSocket()`, `DealerSocket()`, `RouterSocket()`, `PubSocket()`,
  `SubSocket()`, `XPubSocket()`, `XSubSocket()`, and `StreamSocket()` create
  runtime socket implementations.
  `SpotNode()`, and `SpotNodeWithOptions(...)` create service-layer
  implementations.
- `Spot` handles are obtained through `SpotNode.Spot()`,
  `EntrySpot()`, `GetOrCreateSpot(...)`, or `SpotLookup(...)`; direct `Spot`
  construction is not public.
- Actor handles are created through `SpotNode.Actor(...)`; direct Actor
  construction is not public.
- `NewPoller()`, `NewTimer()`, and `NewTimerFromSpot(...)` create eventing
  resources.
- `NewAtomicCounter()`, `NewStopwatch()`, and `NewThread(...)` create utility
  resources owned by the caller.
- Version, capability, strerror, proxy, sleep, and multipart cleanup helpers
  are public contract functions. cgo calls behind those functions stay private.

## Contract Category Map

These categories map to exported identifiers in the aggregate
`bindings/go/contracts/` package. They are review ownership labels, not Go
subpackage names.

- `Core`: context, context options, routing id, version/capability helpers, and
  runtime utility contracts.
- `Messaging`: message, received metadata, topic messages, subscription events,
  stream packet callbacks, and builder payload helpers.
- `Sockets`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `Eventing`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `Service`: SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `Errors`: exported error values or typed error domains.
- Enum, flag, and result identifiers live in the category that defines their
  meaning. Do not create a separate `enums` package just to group declarations
  by syntax.

## Canonical Interface Rules

- Data-plane `Recv`, routed recv, `Subscribe`, and subscription-event receive
  fill caller-provided `*Received`, `*TopicMessage`, or `*SubscriptionEvent`
  values and return `(bool, error)`.
- Part-level receive APIs such as `RecvPart`, `SubscribePart`, and
  `Spot.RecvRoutedPart` are not public contract members. The runtime may use
  `*_part` C substrate internally, but callers receive aggregate
  `Received`/`TopicMessage` values.
- A Spot relay helper, if exposed, consumes one routed Spot message and
  forwards it back to the source Spot route without exposing the payload to the
  caller. It is for relay paths that do not inspect or modify payload data.
  Callers that need payload access use `RecvRouted(...)` and
  `SendToSpot(...)`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return fluent builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- SPOT channel-targeted operations use `SendToChannel(...)` and
  `RequestToChannel(...)`. SPOT topic publish stays `Publish(topic)`.
- Do not add single-payload shortcut methods with the same name as an operation
  start method. `Send(message)`, `Send(routingID, message)`,
  `Publish(topic, message)`, `SendToChannel(channel, message)`, and
  `SendToSpot(..., message)` are not public contract members; callers use
  `Send(...).Message(message).Submit(...)`.
- Multipart payload is accumulated by repeated `Message(...)`,
  `MoveMessage(...)`, or `Bytes(...)` calls. `Bytes(...)` reads the
  caller-owned slice during `Submit(...)` and does not retain it after
  `Submit(...)` returns.
  `Messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in the public package category.
- Dealer sockets must not expose protocol envelope helpers such as
  `RequestFrame(...)` or `Reply(requestToken, parts)`. A dealer can start a
  request through `Request()`, but it cannot reply to an arbitrary token
  because it has no API-level peer routing id.
- Send builders also expose `MoveMessage(...)` for hot paths that can transfer
  ownership at submit time. `Message(...)` keeps the existing contract: the
  caller's message is preserved on submit failure and consumed on success.
  `MoveMessage(...)` is explicit opt-in: after `Submit(...)` returns, the caller
  must not reuse that message even if submit reports an error.
- Message payload factories use Go constructor naming while preserving the
  from-source meaning: `NewMessage(...)` is the primary constructor and
  `NewMessageString(...)` handles UTF-8 string input. `NewMessageFrom(...)` and
  `NewMessageFromBytes` are not part of the public contract.
- Do not add operation-start method families such as `SendNoWait`,
  `PublishWithFlags`, or `RequestAsync`; keep one operation name and let the
  builder absorb the variation. Terminal builder methods may use idiomatic
  names. Pass `context.Context` at submit time, not at builder start.

## Package Shape

Keep the public `contracts` package tree easy to scan.

- Core identifiers cover context, version/capability helpers, options, and
  runtime utilities.
- Messaging identifiers cover message, routing id, received metadata, topic
  message, and subscription event types.
- Socket identifiers cover pair, dealer, router, pub, sub, xpub, xsub, stream,
  options, callbacks, request/reply, publish/subscribe, and stream packet APIs.
- Eventing identifiers cover monitor, monitor snapshot/event, poller, poll
  event, and timer.
- Service identifiers cover SPOT node, SPOT handle,
  topology snapshots, actor refs, actor lifecycle, and operation builders.
- Error identifiers preserve core result domains.
- Enum identifiers cover public enum domains shared across packages.

If a helper exists only to call cgo, manage native memory, or advance request
progress, it is not exported.

## Required Capability Coverage

The Go package must cover these stable user-facing capabilities when the
binding is aligned to the shared .NET-standard policy.

- Context lifecycle, context options, shutdown, auto-HWM recalculation,
  version, capability, and strerror.
- Message ownership, multipart payloads, routing ids, received metadata, topic
  messages, subscription events, and stream packet callbacks.
- All socket families and their typed options.
- Monitor, poller, timer, and readiness semantics.
- SPOT node, SPOT handle, topology snapshots, actors, and
  stream actor binding.

The public Go shape may group or rename APIs idiomatically, but it must not
change the meaning of core operations.

## Spot Get-Or-Create

Go exposes `SpotNode.GetOrCreateSpot(spotRID RoutingID) (*Spot, bool, error)`.
It maps directly to `zlink_spot_node_spot_get_or_new(...)`; it must not be
implemented by composing a lookup path with a separate create path.

The returned `*Spot` is caller-owned and must be closed normally. The boolean
is `true` only for the call that created the logical spot.

## Receive And Subscribe Shape

- Data-plane receive and subscribe APIs must use reusable caller-owned result
  storage.
- Nonblocking no-data must not be confused with hard failure.
- SPOT dispatch readable events are readiness notifications. Callers drain the
  matching receive API until no-data.
- Monitor and timer control-plane APIs may use value-return forms when they
  are more idiomatic and do not add hot-path allocation.
- Service control/admission receive paths such as Actor join request receive may
  use value-return forms such as `(value, bool, error)` or an equivalent typed
  result. They must still distinguish no-data from hard receive failure.

## Error And Validation Policy

- Validate routing ids, actor ids, endpoints, channel names, and topics before
  crossing the cgo boundary.
- Do not silently truncate native fixed-size values.
- Preserve submit, request, recv, handler, close, bind, connect, and config
  error domains.
- Public errors should be inspectable without native errno knowledge.

## Performance Policy

- Hot paths must not use reflection, string-based dynamic dispatch, avoidable
  allocation, avoidable byte-slice copies, hidden sleeps, busy waits, broad
  locks, or goroutine joins.
- cgo bridge code should materialize public Go values directly from the core
  part substrate.
- Do not start one goroutine or timer per request when per-handle progress can
  be shared.
- Perf measurement meaning must match `bindings/c/perf`.

## Implementation Checklist

- Public APIs are exported from the `contracts` package.
- cgo details do not leak into public signatures.
- Resource lifecycle is explicit through `Close`.
- Provider-owned interfaces are used sparingly.
- Exported helper functions and builder convenience methods are declared in the
  matching public contract package, not only in runtime helpers.
- Receive/subscription semantics match the shared binding policy.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf, samples, and public-surface tests use only exported package APIs.
- Implementation-only files or `internal/` packages do not leak through public
  signatures.
- No old aliases, duplicate operation-start names, or deprecated wrappers are
  kept only for compatibility.

Required verification after the Go refactor. Run these commands from
`bindings/go/`:

- Run `go test ./...`.
- Run `./tests/run_tests.sh`.
- Run `./samples/run_samples.sh` when public examples or construction paths
  changed.
- Run `./perf/run_benchmarks.sh` and `./perf/run_benchmarks_multi.sh` as smoke
  gates when hot path, receive, send, request, poller, timer, or service
  behavior changed.
- Run `go vet ./...` when available for the changed packages.
- Search samples, perf, public-surface tests, and `contracts` for imports or
  references to implementation-only packages, cgo bridge helpers, raw handles,
  or native bridge symbols. Implementation tests must stay scoped to private
  packages and must not become examples of consumer imports.

## Actor And Spot Route Results

Go exposes Actor and Spot route lookup results through exported value types.

- `ActorRoute` preserves the resolved Actor ref, Actor node RID, current Spot
  RID, and current Spot kind.
- `SpotRoute` preserves Spot RID, owner node RID, and Spot kind.
- `SpotKind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- SpotNode snapshot entries expose the same Spot kind/current Spot fields as the
  core snapshots.

Go exposes `SpotNode.SendToActor(ActorRef)` and
`SpotNode.RequestToActor(ActorRef)` for resolved Actor refs. The send operation
consumes one or more message parts on successful submit and completes when the Actor owner
mailbox accepts the handoff. The request operation consumes request parts on
successful submit and delivers the Actor handler reply parts. Go must not
reintroduce the removed Discovery route table or resolver APIs as compatibility
helpers.
