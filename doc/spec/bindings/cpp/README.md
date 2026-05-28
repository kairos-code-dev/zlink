[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# C++ Binding Final Structure

This document defines the completed C++ library shape after the binding
refactor. It is not an exhaustive list of every method. The concrete public
contract is
`bindings/cpp/include/zlink/Contracts/`.

In the completed structure, `Contracts/`, installed header projections, tests,
samples, perf runners, and runtime behavior all map the stable capabilities of
`core/include/zlink.h` into C++-idiomatic types.

This README is the C++ binding's final-state interpretation of the shared
policy in `../README.md`. The completed C++ binding does not keep old include
paths, alias methods, alternate projections, or wrapper layers only to preserve
the previous surface. This document is the acceptance standard for the
refactored C++ binding.

This binding follows the shared bindings architecture map with C++ naming:
`Contracts/` owns the public contract categories and `Runtime/` owns
implementation helpers. The folder names are C++ header organization, not
namespace segments that users should depend on.

## Public Contract Source

- Public contract: `bindings/cpp/include/zlink/Contracts/`.
- Runtime implementation: `bindings/cpp/include/zlink/Runtime/`.
- Public entrypoint projection: `bindings/cpp/include/zlink.hpp`.
- Installed projection: `bindings/cpp/include/zlink/Contracts/...` and
  deliberate installed helper headers under `bindings/cpp/include/zlink/...`.
- Namespace: all public types live under `zlink`; service types live under
  `zlink::service`.
- Internal implementation: native bridge helpers, callback trampolines, request
  progress helpers, non-public `detail` helpers, and runtime headers under
  `bindings/cpp/include/zlink/Runtime/`.
- Documentation role: this README defines the shape, boundaries, and required
  semantic coverage. `Contracts/` owns the exact member list; installed headers
  must project it intentionally.

C++ is a header-only binding in this repository. Do not create a second
`bindings/cpp/src/zlink/Contracts/` tree. The installed include tree is both
the build input and the public projection. Do not copy a Java or .NET
interface-heavy layout into C++; C++ uses installed headers, RAII classes,
concrete values, and private/detail helper placement as its natural boundary.

## Repository Layout

The completed C++ binding uses these paths consistently.

- Public contract: `bindings/cpp/include/zlink/Contracts/`.
- Runtime implementation: `bindings/cpp/include/zlink/Runtime/`.
- Native bridge/artifacts: `bindings/cpp/include/zlink/Runtime/Native/` when
  C++ native bridge declarations are needed, and `bindings/cpp/native/` for
  packaged native binaries.
- Public entrypoint: `bindings/cpp/include/zlink.hpp`.
- Codec extensions: `bindings/cpp/codecs/`.
- Tests: `bindings/cpp/tests/`.
- Samples: `bindings/cpp/samples/`.
- Perf: `bindings/cpp/perf/`.

`Contracts/` and `Runtime/` are fixed repository folders under
`bindings/cpp/include/zlink/`. The `zlink` namespace and `zlink.hpp` are the
C++ projection of that contract. Do not expose `Contracts` or `Runtime` as
namespace segments.

Because the binding is header-only, runtime helper headers are physically
installed, but they are not public API. Public samples, perf, and tests include
`<zlink.hpp>` and use the projected `zlink` API, not runtime helper paths.
Wrapper headers such as `include/zlink/message.hpp`,
`include/zlink/services/spot.hpp`, or `include/zlink/sockets/dealer.hpp` are
not part of the completed layout. The completed tree does not replace them with
forwarding headers.

Monitor, poller, and timer contracts live under the shared `Eventing/`
category. `Contracts/Monitoring/` is not part of the completed public contract,
and the completed tree does not keep `Monitoring/` forwarding headers.

File granularity follows the common policy in `../README.md`: keep one file
per independent public concept or tight operation/model group. Very small
marker, delegate, enum, or pass-through helper files should be merged into the
nearby contract file when that makes the public shape easier to read.

## Public Contract At A Glance

The completed C++ binding makes the public contract visible without adding
interface-only layers. A user can start at `<zlink.hpp>`, then follow this map
to the owning contract header.

| Area | Public objects and roles | Owning contract header |
|------|--------------------------|------------------------|
| Core | `context_t`, context options, routing id, version/capability helpers | `Contracts/Core/` |
| Messaging | `message_t`, `received_t`, `topic_message_t`, `subscription_event_t`, multipart helpers | `Contracts/Messaging/` |
| Sockets | `pair_socket_t`, `dealer_socket_t`, `router_socket_t`, `pub_socket_t`, `sub_socket_t`, `xpub_socket_t`, `xsub_socket_t`, `stream_socket_t`, send/recv/request/reply builders | `Contracts/Sockets/` |
| Eventing | `monitor_handle_t`, monitor events, poller, poll event, timer, readiness helpers | `Contracts/Eventing/` |
| Service | `registry_t`, `discovery_t`, `spot_node_t`, `spot_t`, `actor_ref_t`, actor lifecycle models, service operation builders | `Contracts/Service/` |
| Errors | public exception and result-domain types | `Contracts/Errors/` |

The map above is the public API index. It is the C++ equivalent of a contract
surface overview; it does not imply `IContext`, `ISpot`, `IActor`, or similar
abstract interfaces. Public resource objects remain concrete RAII facades unless
callers need true substitutable behavior. Narrow interfaces are allowed only for
roles that users naturally replace, such as codecs, callbacks, handlers, or poll
targets.

The public contract is read in two steps:

1. Start at `<zlink.hpp>` to see the public contract categories included by the
   C++ binding.
2. Open the owning `Contracts/...` header to inspect the concrete public type
   and its public member list.

For example, the completed SPOT surface is visible as a concrete facade, not as
an interface/implementation pair:

```cpp
namespace zlink::service {

class spot_t {
public:
    spot_t(spot_t&&) noexcept = default;
    spot_t(const spot_t&) = delete;

    send_op_t send();
    reply_op_t reply();
    int recv(received_t& out, recv_flags_t flags = recv_flags_t::none);
    void set_send_ready_handler(std::function<void()> handler);
    void close();
};

} // namespace zlink::service
```

`zlink.hpp` acts as the public table of contents for those facades:

```cpp
#include "zlink/Contracts/Core/context.hpp"
#include "zlink/Contracts/Messaging/message.hpp"
#include "zlink/Contracts/Messaging/received.hpp"
#include "zlink/Contracts/Sockets/dealer.hpp"
#include "zlink/Contracts/Sockets/router.hpp"
#include "zlink/Contracts/Eventing/poller.hpp"
#include "zlink/Contracts/Service/spot_node.hpp"
#include "zlink/Contracts/Service/spot.hpp"
#include "zlink/Contracts/Service/actor.hpp"
#include "zlink/Contracts/Errors/error.hpp"
```

Runtime details stay behind the facade. A contract header may use a detail helper
because this binding is header-only, but users should not learn or include that
helper directly:

```cpp
namespace zlink::detail {

class spot_submit_state_t {
    // Owns native submit state, part loops, callback userdata, and errno mapping.
};

} // namespace zlink::detail
```

This structure keeps the public surface easy to scan while preserving C++
ownership semantics. `spot_t`, `spot_node_t`, and `actor_ref_t` are the contract;
`Runtime/...` and `zlink::detail` are implementation support.

```text
bindings/cpp/
+-- include/
|   +-- zlink.hpp
|   +-- zlink/
|   |   +-- Contracts/
|   |   |   +-- Core/
|   |   |   +-- Messaging/
|   |   |   +-- Sockets/
|   |   |   +-- Eventing/
|   |   |   +-- Service/
|   |   |   +-- Errors/
|   |   +-- Runtime/
|   |   |   +-- Core/
|   |   |   +-- Messaging/
|   |   |   +-- Sockets/
|   |   |   +-- Eventing/
|   |   |   +-- Service/
|   |   |   +-- Errors/
|   |   |   +-- Native/
+-- native/
+-- codecs/
+-- tests/
+-- samples/
+-- perf/
```

## Core Capability Ownership

Every stable core capability exposed by C++ follows these ownership rules:

1. Add the public type or method to the correct
   `bindings/cpp/include/zlink/Contracts/` category.
2. Update `bindings/cpp/include/zlink.hpp` and any deliberate installed
   projection header.
3. Decide the C++ domain owner: context, message, socket, monitor, timer,
   service, SPOT, actor, error, or option.
4. Keep raw C handle access, `*_part` loops, callback userdata, trampoline
   state, and native marshalling helpers in `Runtime/` headers.
5. Add public-header tests and at least one sample/perf update when the new
   capability affects user workflows or measurement.
6. Check that the new public API is not just a shallow C wrapper. If it only
   forwards without improving ownership, validation, or shape, keep it
   internal.
7. Obsolete public names are absent. Deprecated aliases, forwarding overloads,
   and alternate public headers are absent unless a later document explicitly
   changes this C++ policy.

For explicit Spot routing-id acquisition, the C++ binding exposes
`spot_node_t::get_or_create_spot(routing_id_t)` and maps it directly to
`zlink_spot_node_spot_get_or_new(...)`. It returns the owned `spot_t` facade
and the creation flag. Do not implement this behavior by composing
`spot_lookup()` and `create_spot()`.

## Library Shape

The C++ binding should feel like a small native C++ library over the core C
contract.

- Public resource objects are RAII classes that own or borrow native handles
  according to their documented lifetime.
- Destructors release resources without requiring callers to know native close
  sequencing.
- Public methods use `snake_case`.
- Public value types such as message, routing id, received metadata, topic
  message, result, error, enum, and option types stay concrete.
- Use templates, overloads, and move semantics only when they simplify caller
  ownership or avoid copies. Do not expose template machinery as a substitute
  for a clear domain type.
- Use virtual interfaces only when callers need substitutable behavior. Do not
  wrap every handle in an abstract interface by default.
- Operation builders are required for multipart send, publish, request, reply,
  actor, and SPOT operations so native request state stays hidden and ownership
  stays clear.

## Contract / Runtime Placement Rules

- Public declarations and user-visible behavior belong in `Contracts/`.
- Public free functions, static helpers, extension-style helpers, and builder
  convenience helpers belong in `Contracts/` when users can call them directly.
- Runtime handle owners, socket kernels, request pumps, callback trampolines,
  and part-loop helpers belong in `Runtime/`.
- FFI declarations, raw C handles, native struct mirrors, marshalling helpers,
  and platform loading code belong in `Runtime/Native/`.
- `zlink.hpp` must project `Contracts/`, not make `Runtime/` helper paths the
  public include style.
- If a runtime concrete class is directly constructible, its public behavior
  must still be described by `Contracts/`.
- Contract headers may include implementation helpers only when header-only C++
  mechanics require it. Those helpers must stay under `zlink::detail` or another
  non-public implementation boundary and must not become documented user entry
  points.

## Contract Folder Layout

`Contracts/` is the source ownership map for public C++ declarations.
`zlink.hpp` projects these categories into the `zlink` namespace.

- `Core/`: context, context options, routing id, utility resources, and public
  free functions such as version or capability helpers.
- `Messaging/`: message, received metadata, topic messages, subscription
  events, stream packet callbacks, and builder payload helpers. Codec helpers
  are separate extension packages under `bindings/cpp/codecs/`, not undeclared
  placeholders in the core binding.
- `Sockets/`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `Eventing/`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `Service/`: registry, discovery, SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `Errors/`: exception or typed error-result domains.
- Enum, flag, and result types live in the category that defines their meaning.
  Do not create an `Enums/` folder just to group declarations by syntax.

## Canonical Interface Rules

- Data-plane `recv`, routed recv, subscribe, and subscription-event receive use
  caller-provided output storage such as `received_t&`,
  `topic_message_t&`, or `subscription_event_t&`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return move-only fluent builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- SPOT channel-targeted operations use `send_to_channel(...)` and
  `request_to_channel(...)`. SPOT topic publish stays `publish(topic)`.
- Handler registration methods use `set_..._handler` names. For example, send
  readiness uses `set_send_ready_handler(...)`, raw STREAM packet handling uses
  `set_packet_handler(...)`, monitor events use `set_event_handler(...)`, and
  SPOT dispatch uses `set_dispatch_handler(...)`.
- `on_...` names are not public registration methods in the completed C++ API.
  They are reserved for internal or protected hooks if such hooks are needed.
- Do not add single-payload shortcut overloads with the same name as an
  operation start method. `send(message)`, `send(routing_id, message)`,
  `publish(topic, message)`, `send_to_channel(channel, message)`, and
  `send_to_spot(..., message)` are not public contract members; callers use
  `send(...).message(message).submit()`.
- Multipart payload is accumulated by repeated `message(...)` calls.
  `messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in `Contracts/`.
- Dealer sockets must not expose protocol envelope helpers such as
  `request_frame(...)` or `reply(request_token, parts)`. A dealer can start a
  request through `request()`, but it cannot reply to an arbitrary token
  because it has no API-level peer routing id.
- Do not add operation-start overload families such as `send_no_wait`,
  `publish_with_flags`, or `request_async`; keep one operation name and let
  the builder absorb the variation. Terminal builder methods may use idiomatic
  names such as `submit_async`.
- Do not keep canonical-name bypasses such as `on_send_ready(...)`,
  `on_packet(...)`, `on_event(...)`, or operation aliases. Call sites use the
  canonical public contract instead of layered aliases.

## Capability Coverage

The public headers cover these groups in the completed C++ binding.

- Core runtime: context, version/capability helpers, context options, shutdown,
  and auto-HWM recalculation.
- Messaging: message ownership, builder multipart input, received metadata, topic
  messages, subscription events, routing ids, and callback types.
- Socket families: pair, dealer, router, pub, sub, xpub, xsub, stream, common
  options, typed socket options, bind/connect/disconnect, TLS, callbacks, and
  request/reply surfaces.
- Eventing: socket monitor, monitor event, monitor snapshot, poller, poll
  event, timer, and readiness flags.
- Services: registry, discovery, SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and actor operations.
- Errors: typed exception or error-result surfaces that preserve the core
  result domains.

The C++ surface should not expose raw native handles, `*_part` loops, callback
userdata, internal inproc endpoints, or request pump objects as public concepts.

## Lifetime And Ownership

C++ callers should not have to reason about C handle cleanup.

- Resource classes release their native handle in their destructor and support
  explicit `close` or equivalent lifecycle methods when the handle can fail to
  close.
- Move-only resource classes are preferred over shared mutable handle
  ownership.
- Message values should support efficient move and explicit copy when copying
  is requested.
- Data-plane receive and subscribe paths use caller-provided storage.
- Service control/admission receive paths such as Actor join request receive may
  use optional or typed result-return forms when that is clearer for C++ callers.
  They must still distinguish no-data from hard receive failure.
- Callbacks must keep native callback lifetime and user callable lifetime
  internally consistent.

## Error And Result Policy

The binding may use exceptions or typed result objects, but the public shape
must preserve core semantics.

- No-data and temporary backpressure remain distinct from hard failures.
- Request, submit, recv, bind, connect, config, handler, and close failures
  keep their result-domain meaning.
- `pollout` is a send-recovery readiness signal, not a generic writable bit.
- ROUTER/PUB defaults, SPOT HWM defaults, and SPOT dispatch worker semantics
  follow the core header.

## Performance Policy

- Build multipart values directly from the core part substrate.
- Avoid unnecessary heap allocation, avoidable copies, reflection-like dynamic
  dispatch, hidden waits, sleeps, busy waits, broad locks, and joins in hot
  paths.
- Perf and samples must include installed public headers only.
- The C++ perf meaning must match `bindings/c/perf`: same pattern semantics,
  same transport meaning, same client-count policy, and no private fast path.

## Completed Structure Requirements

The completed C++ binding satisfies these requirements:

- Installed headers expose all stable user-facing core capabilities.
- `Contracts/Eventing/` is the only public eventing category. `Contracts/Monitoring/`
  is gone, and `zlink.hpp` includes the Eventing headers.
- Old wrapper include paths are gone. Applications, samples, perf, and tests
  include `<zlink.hpp>` or deliberate `Contracts/...` headers only.
- Public headers are enough for applications, perf, samples, and framework
  adapters.
- Private helper headers are not needed by users.
- Value types remain concrete unless abstraction removes real complexity.
- Public APIs hide native part loops, raw handles, and callback userdata.
- Handler registration uses `set_..._handler` names, with no public `on_...`
  aliases.
- Public helper/free functions and builder convenience methods are declared in
  `Contracts/`, not only in runtime helpers.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf tests use the same measurement meaning as C perf.

## Actor And Spot Route Results

C++ exposes Actor and Spot route lookup results as concrete contract types.

- `actor_route_t` preserves the resolved Actor ref, `actor.node_rid`,
  `current_spot_rid`, and `current_spot_kind`.
- `spot_route_t` preserves `spot_rid`, `owner_node_rid`, and `spot_kind`.
- `spot_kind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- `spot_node_spot_entry_t` and `spot_node_actor_entry_t` expose the same Spot
  kind/current Spot fields as the core snapshots.

C++ must not introduce ROUTER-to-Actor or Actor-to-ROUTER direct messaging
methods. Applications compose `discovery_t::resolve_actor()` or
`discovery_t::resolve_spot()` with the Spot routed APIs.

## SpotNode Router Channel Peers

C++ exposes router channel peer wiring as public `spot_node_t` methods:
`connect_router_channel_peer(channel_name, endpoint)`,
`disconnect_router_channel_peer(channel_name, endpoint)`,
`disconnect_router_channel_peer_rid(channel_name, peer_rid)`, and
`attach_spot_route_channel_discovery(channel_name, discovery)`. The methods map
directly to the matching core C APIs and preserve the core error categories.
