# Python binding Core 11 public contract

This document defines the Core 11 raw messaging contract exposed by the
`zlink` Python package. A behavior not present in the current implementation
and public header is not part of this contract. Python 3.9 and later are
supported, and the current candidate package version is `11.2.0`.

## Scope

The binding projects these Core resources into Python objects and Protocols:

| Area | Public concepts |
|---|---|
| Core | `Context`, `ContextOptions`, `Message`, `Received`, `RoutingId` |
| Socket | PAIR, DEALER, ROUTER, STREAM, PUB, SUB, XPUB, XSUB |
| Eventing | `MonitorSocket`, `MonitorEvent`, `MonitorStatus`, `Poller`, `PollEvents`, `Timer` |
| Utility | `AtomicCounter`, `Stopwatch`, `Thread`, `proxy`, `sleep` |
| Results | `SubmitResult`, `RequestResult`, `RecvResult`, `ConfigResult`, and matching errors |

Sockets preserve Core raw endpoint and message-routing semantics. Native
handles, FFI symbols, and native structs are not public Python types.

## Package surface

Callers use factories and contract types from the `zlink` package root. The
implementation modules are private; callers do not import `_native` or
`_runtime`. The package root does not provide a compatibility alias for a
concept outside the Core raw contract.

The primary factories are `create_context()`, the raw socket factories,
`create_poller()`, `create_timer()`, `create_received()`, and the message
factories. Exact Python signatures are owned by the contract modules and are
interpreted against the public Core header.

## Ownership and lifetime

- `Context` owns the native context and releases it through `close()` or a
  context-manager exit.
- Each socket, monitor, poller, and timer owns the native handle it creates.
  Calls after a successful `close()` are invalid.
- `Message.from_(value)` creates an independent native message from the
  caller's value. After a successful send submit, native message-part
  ownership moves into the Core send path.
- `Received` is caller-provided receive storage. A successful `recv_into`
  fills its parts and routing metadata; closing it releases those native parts.
- Native views exposed by `Received.parts` are valid only while their owner is
  open. Use `to_bytes()` or `to_bytes_list()` when a value must outlive it.
- Callback references remain retained until the native callback registration
  no longer needs them. Callback failures follow the binding callback policy.

## Send, receive, and no-data

Send builders add message parts and then call `submit()`. Blocking send follows
the socket timeout options and the Core contract. A caller-provided receive
using `RecvFlags.DONT_WAIT` returns `False` when no message is available.
Control APIs that return a pending value, such as timers and monitors, return
`None` when no value is available. Native failures are reported through the
corresponding error type rather than being hidden as no-data.

DEALER and ROUTER request/reply preserve Core routing metadata and request
sequence values. `Received.routing_id` is a raw routing id and is not converted
to another identity type. The current single-part accessor is
`single_part_or_throw()`, matching the implementation and contract tests. A
rename requires approval of its separate draft first.

## Errors

Calls that represent Core results expose `result`, `code`, and `native_errno`
on the matching Python error. Input-shape checks may fail before a native call,
but a native operation failure is not rewritten as a generic `ValueError`.
`SubmitError`, `RequestError`, `RecvError`, `BindError`, `ConnectError`,
`ConfigError`, `CloseError`, and `HandlerError` derive from `ZlinkError`.

## Python version and type package

Public annotations use expressions that Python 3.9 can parse and evaluate. The
package contains `py.typed`. The public contract type gate targets Python 3.9
and the `src/zlink/contracts` tree specified by `pyrightconfig.json`.

## Related documents

- Usage patterns are described in the [Python guide](../../guide/python/index.ko.md).
- `core/include/zlink.h` and the Core spec own Core function and layout meaning.
- Native lifetime and callback implementation details belong in internals
  documentation, not in this public contract.
