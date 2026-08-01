[English](README.en.md) | [한국어](README.ko.md)

[Spec Index](https://kairos-code-dev.github.io/zlink/en/spec/) · [Bindings Policy](../README.en.md)

# C Binding Implementation Blueprint

This document defines how the C binding must be shaped. It is not a second
copy of every public function signature. The concrete public API contract is
`core/include/zlink.h`.

For C, the native ABI is the binding contract. `bindings/c` does not add a
second contract/runtime layer over the core C API. A C implementation is
considered aligned when the public header, native library behavior, tests,
samples, packaging, and perf runners agree with the rules in this document and
with `core/include/zlink.h`.

The shared binding architecture map still applies as a review vocabulary:
core, messaging, sockets, eventing, service, and errors are the conceptual
areas reviewers use when reading the C header. C expresses those areas through
header sections, type/function prefixes, tests, samples, and documentation
sections instead of separate `contracts/` and `runtime/` folders.

The shared file granularity policy is review vocabulary for C header sections
and helper files only. It does not require wrapper-style `contracts/` or
`runtime/` folders because `core/include/zlink.h` remains the ABI baseline.

## Public Contract Source

- Public contract: `core/include/zlink.h`.
- Public ABI: exported `zlink_*` functions, public structs, enums, constants,
  callback typedefs, and ownership rules declared by that header.
- Internal implementation: files under `core/src/`, private helper headers,
  generated bridge files, build scripts, and test helpers.
- Documentation role: this README describes the C shape and review rules. The
  header owns the exact binding signature list.

Do not introduce another C facade above `zlink.h`. Local alias functions,
alternate option bags, or compatibility wrappers that only forward to another
`zlink_*` function are not part of the binding contract.

## Repository Layout

Use these paths consistently when changing the C binding.

- Public contract: `core/include/zlink.h`.
- Runtime implementation: `core/src/`.
- Native artifacts: `core/build`.
- Binding include projection: `bindings/c/include/`, when packaging needs
  installed headers.
- Tests: `bindings/c/tests/`.
- Samples: `bindings/c/samples/`.
- Perf: `bindings/c/perf/`.
- C API mapping, sample/test support, and perf policy live under `bindings/c/`.

Temporary build directories and generated output are not contract locations.

```text
zlink/
+-- bindings/
|   +-- c/
|   |   +-- include/
|   |   +-- tests/
|   |   +-- samples/
|   |   +-- perf/
+-- core/
|   +-- include/
|   |   +-- zlink.h
|   +-- src/
|   +-- build/
```

## API Change Workflow

When adding or changing a C capability:

1. Add or update the public declaration in `core/include/zlink.h`.
2. Implement the behavior under `core/src/`.
3. Update errno/result documentation when the result domain changes.
4. Add tests that include only public headers.
5. Update samples only when the user-facing shape changes.
6. Update perf runners only when measurement behavior changes.
7. Rebuild `core/build` before interpreting C perf results.

## Library Shape

The C binding keeps the native ABI shape.

- Function names use `zlink_*` and `snake_case`.
- Blocking and nonblocking behavior is selected by flags such as
  `ZLINK_DONTWAIT`, not by separate `try_*` public functions.
- Send paths return `zlink_submit_result_t` or the documented request result.
- Receive paths return `zlink_recv_result_t` and fill caller-owned output
  storage according to the header contract.
- Multipart payloads use repeated `zlink_msg_t *part` calls plus
  `zlink_part_flag_t`.
- Routed APIs use explicit routing id parameters and explicit output routing
  id storage.
- Callback APIs expose C function pointers and userdata only where the public
  header declares them.

Higher-level object conveniences such as `Received.Reply(...)`,
`Socket.Send().Message(...).Submit()`, or `Spot.Publish(topic)` do not apply
to C. Those shapes belong to higher-level bindings.

## Interface Shape Exception

C is the ABI baseline and does not adopt the wrapper binding interface rules.

- Receive and subscribe use the output parameters declared in `zlink.h`.
- Send, publish, request, and reply use explicit `zlink_*` functions and flags.
- C does not expose operation builders, staged interfaces, or fluent helper
  objects.
- Wrapper binding rules for public static facades, builder convenience helpers,
  and contract/runtime folders do not apply to C. If a C helper is public, it
  must be declared in `core/include/zlink.h`; otherwise it is internal.
- C samples and perf include the public header and call the public C ABI
  directly.

## Required Capability Coverage

A C review checks these groups in `core/include/zlink.h`.

- Runtime, version, capability, context lifecycle, and context options.
- Message lifecycle, message data access, copy/move/adopt rules, and property
  lookup.
- Socket lifecycle, bind/connect, disconnect, options, TLS helpers, routing id,
  send, receive, request, reply, publish, subscribe, and stream APIs.
- Eventing APIs: monitor, poller, timer, callback registration, and readiness
  semantics.
- SPOT node, SPOT handle, topology snapshot, actor, and service-layer APIs.
- Error/result enums and errno mapping.

When a capability exists in `core/include/zlink.h`, the C binding exposes it
directly through the public header. When a capability is not in that header, it
is not public C API.

## Spot Get-Or-New

`bindings/c/include/zlink.h` exposes
`zlink_spot_node_spot_get_or_new(...)` with the same signature and result
contract as the core public header. The function atomically gets or creates a
local logical Spot by routing id and returns both a caller-owned `Spot` facade
handle and the created flag.

This API does not join an actor to the Spot. Join remains a separate service
operation.

## Ownership And Lifetime

C callers own memory explicitly. The header must make ownership transfer clear
at every boundary.

- `zlink_msg_t` values must be initialized before use and closed exactly once.
- APIs that move or adopt message storage must document the source object's
  state after the call.
- Receive APIs fill caller-provided storage. The caller closes any message
  parts that become owned by the caller.
- Handles are closed through their matching `zlink_*_close`,
  `zlink_*_destroy`, `zlink_ctx_term`, or documented lifecycle function.
- Callback registration must not require callers to know private worker,
  socket, or inproc endpoint details.

## Error And Result Policy

The C binding reports public results through C result domains, not exceptions.

- Temporary no-data is reported through the documented recv result.
- Temporary backpressure is reported through the documented submit result.
- Configuration, bind, connect, close, request, handler, and recv failures map
  to the result and errno rules in `zlink.h`.
- The public header must not require callers to inspect private implementation
  state to classify a failure.

## Performance Policy

The C binding is the performance baseline for other bindings.

- Hot paths must not add aggregate materialization when the public part
  substrate can stream parts directly.
- Do not add hidden sleeps, busy waits, thread joins, reflection-like dynamic
  dispatch, coarse global locks, or avoidable copies on send/recv, request,
  dispatch, poller, timer, stream, SPOT, or actor paths.
- Perf runners and samples must include only public headers.
- `bindings/c/perf` must measure the runtime from `core/build` unless the perf
  policy explicitly says otherwise.

## Implementation Checklist

Before declaring the C binding aligned:

- `core/include/zlink.h` declares the exact public C ABI.
- Header documentation and errno/result documentation agree.
- Public tests and samples compile without private headers.
- Higher-level bindings can implement their public shapes without calling
  private C helpers.
- Perf output prints the runtime library path and does not run against a stale
  `core/build` runtime.

## Actor And Spot Route Results

The C binding exposes the core route result structs exactly as public ABI.

- `zlink_actor_route_t` contains the resolved Actor ref, including
  `actor.node_rid`, plus `current_spot_rid` and `current_spot_kind`.
- `zlink_spot_route_t` contains the requested `spot_rid`, the
  `owner_node_rid`, and `spot_kind`.
- `zlink_spot_kind_t` distinguishes Entry Spot from user Spot. Invalid kind is
  not a successful Actor or Spot route result.
- C samples that route by Actor id resolve the Actor first, then pass
  `actor.node_rid` and `current_spot_rid` to the existing Spot routed APIs.

The C binding must not add `zlink_router_send_actor`,
`zlink_router_request_actor`, or Actor-to-ROUTER request helpers. Actor-directed
delivery remains a route lookup followed by existing Spot routed send/request.
