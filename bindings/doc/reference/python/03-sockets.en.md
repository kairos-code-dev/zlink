[한국어](03-sockets.ko.md) | English

[Reference index](README.en.md)

# 03. Sockets

This category covers `_SocketContract` (the shared base `Protocol`), `CommonSocketOptions` and its
per-type extensions, the eight concrete socket `Protocol` types, and the send/request/reply
operation-builder family (declared here, not in Messaging — see the README). The exact signatures
are owned by
[`contracts/sockets/`](../../../../bindings/python/src/zlink/contracts/sockets/).

---

## `_SocketContract` shared base (private by convention)

The base `Protocol` every socket type extends: binding, disposal, options. Named with a leading
underscore — Python's convention for "not public API," even though `socket.py`'s module-level
`__getattr__` is how every concrete socket type is actually reached from this package.

```python
socket.bind("tcp://*:5555")
with socket:
    ...
```

**Options.** `bind(endpoint)`, `close()`, `options` (property — the typed options facade for this
socket type), `__enter__`/`__exit__` (sync context-manager only — **no `__aenter__`/`__aexit__`
here**, unlike the async-and-sync-both pattern every other resource type in this binding follows).
**No `unbind`, no TLS methods (`set_tls_server`/`set_tls_client`/etc.) at all** — unlike every
other language covered so far, this binding's socket base contract has neither.

**Completion result.** `bind`/`close` are synchronous with no return value.

**When to use.** Every concrete socket type below extends this Protocol and adds its own
`connect`/`disconnect`/send/recv surface.

---

## `CommonSocketOptions` and per-type extensions

The typed options facade shared by every socket type, reached via `socket.options`.

```python
socket.options.send_high_water_mark = 100_000
socket.options.linger_ms = 1000
socket.options.submit_retry_mode = SubmitRetryMode.LOCAL_FAILURE
```

**Options.** `linger_ms`, `send_high_water_mark`/`receive_high_water_mark`, `send_timeout_ms`/
`receive_timeout_ms`, `immediate`, `rid_duplicate_policy`, `connect_timeout_ms`, `ipv6`,
`tcp_no_delay`, `tcp_keepalive`, `max_message_size`, `backlog`, `reconnect_interval_ms`/
`reconnect_interval_max_ms`, `submit_retry_mode`, `submit_retry_timeout_ms`,
`submit_retry_attempts` — all plain get/set properties. **Three heartbeat properties exist here
that no other language covered so far exposes**: `heartbeat_interval_ms` (interval between
heartbeat pings on an idle connection), `heartbeat_ttl_ms` (how long the remote keeps the
connection alive without a heartbeat), `heartbeat_timeout_ms` (how long to wait for a heartbeat
reply before treating the connection as dead). Per-type extensions: `DealerSocketOptions` adds
`probe`, `weight`, `request_timeout_ms`. `RouterSocketOptions` adds `mandatory`, `handover`,
`probe`, `connect_routing_id` (the *only* routing-id-shaped surface anywhere in this binding's
socket contracts — see the README's note that no socket type has `set_routing_id`/`get_routing_id`
of its own), `weight`, `request_timeout_ms`. `StreamSocketOptions` adds `notify`. `PubSocketOptions`
adds `verbose`, `verboser`, `manual`, `no_drop`, `manual_last_value`, `welcome_message`,
`topics_count` (read-only), `approve_subscribe(routing_id)`/`reject_subscribe(routing_id)`.
`SubSocketOptions` adds only `topics_count` (read-only).

**Completion result.** Every property read/write is synchronous.

**When to use.** Set `send_high_water_mark`/`receive_high_water_mark` and `linger_ms` before the
socket starts exchanging messages when the defaults don't fit the deployment. Use the three
`heartbeat_*` properties to tune ZMTP-level liveness detection independent of the transport's own
TCP keep-alive.

---

## `PairSocket`

An exclusive one-to-one peering socket with no routing.

```python
pair = create_pair_socket(ctx)
pair.send().message(b"ping").submit()
received = create_received()
if pair.recv_into(received):
    ...
```

**Options.** `connect(endpoint)`, `disconnect(endpoint)`, `disconnect_rid(peer_rid)`, `send()`
(starts the shared `SendOp` builder), `recv_into(received, *, flags=0)` (returns `bool`), plus the
`_SocketContract` base surface.

**Completion result.** `recv_into` returns `False` only when `DONT_WAIT` is set and no message is
available.

**When to use.** Use PAIR for an exclusive point-to-point link — it has no peer routing and does
not load-balance.

---

## `DealerSocket`

Load-balances sends across its connected peers and can issue routed requests.

```python
dealer = create_dealer_socket(ctx)
dealer.request().message(b"payload").submit(callback)
```

**Options.** `dealer_options` (property, returns `DealerSocketOptions`), `connect(endpoint)`,
`disconnect(endpoint)`, `send()`, `request()` (starts the shared `RequestOp` builder — no target
parameter, since DEALER has no API-level peer routing id), `recv_into(received, *, flags=0)`. **No
`set_routing_id`/`get_routing_id` at all** — see the README's note.

**Completion result.** `recv_into` follows the same `False`-on-`DONT_WAIT` convention as
`PairSocket`.

**When to use.** DEALER has no protocol envelope helper to reply to an arbitrary token — reply from
a received request context (`Received.reply()`, Messaging category) or an explicit ROUTER reply
surface instead.

---

## `RouterSocket`

Routes messages to peers addressed by routing id, and can reply to a specific peer's request.

```python
router = create_router_socket(ctx)
router.send(peer_rid).message(b"hello").submit()
```

**Options.** `router_options` (property, returns `RouterSocketOptions`), `connect(endpoint)`,
`disconnect(endpoint)`, `send(routing_id)`, `request(routing_id)` (Messaging category's shared
`RequestOp`, addressed to a specific peer), `reply(routing_id, request_seq)` (the shared `ReplyOp`),
`recv_into(received, *, flags=0)`. **This binding declares no `try_send_completion_control`/
`set_completion_control_handler`** — the opaque Completion-control surface documented on ROUTER in
dotnet/cpp/java/node has no equivalent here, matching rust's absence of the same surface.

**Completion result.** `recv_into` follows the convention above.

**When to use.** Use `request(routing_id)`/`reply(routing_id, request_seq)` for ROUTER-initiated or
ROUTER-answered request/reply, where DEALER cannot address a specific peer.

---

## `PubSocket` / `SubSocket` / `XPubSocket` / `XSubSocket`

PUB publishes topic-filtered messages, dropping ones with no matching subscriber; SUB subscribes
with subscriptions set as socket options; XPUB/XSUB add subscriber-event surfacing and
message-carried subscriptions respectively.

```python
pub = create_pub_socket(ctx)
pub.publish("prices").message(tick).submit()

sub = create_sub_socket(ctx)
sub.set_subscription("prices.")
msg = create_topic_message()
if sub.subscribe_into(msg):
    ...
```

**Options.** `PubSocket`: `pub_options`, `connect(endpoint)`, `disconnect(endpoint)`,
`publish(topic)` (starts the shared `SendOp` builder). `SubSocket`: `sub_options`,
`connect(endpoint)`, `disconnect(endpoint)`, `set_subscription(topic)`/`unset_subscription(topic)`
(subscriptions accumulate), `subscription_at(index)` (returns `(filter, is_pattern)` tuple, or
`None`), `subscribe_into(topic_message, *, flags=0)`. `XPubSocket`: same shape as `PubSocket`
(`pub_options`, `connect`, `disconnect`, `publish`) plus `receive_subscription_event_into(event, *,
flags=0)`. `XSubSocket`: an **entirely independent `Protocol` declaration with the identical member
set to `SubSocket`** — `sub_options`, `connect`, `disconnect`, `set_subscription`/
`unset_subscription`, `subscription_at`, `subscribe_into` — no shared base type links the two
beyond the matching shape.

**Completion result.** `subscribe_into`/`receive_subscription_event_into` follow the
`False`-on-`DONT_WAIT` convention above.

**When to use.** Use `XPubSocket` specifically to observe subscriber churn via
`receive_subscription_event_into`, or manual admission via `PubSocketOptions.manual`/
`approve_subscribe`/`reject_subscribe`. Use `XSubSocket` specifically when subscriptions must be
carried as ordinary messages instead — the choice is entirely about which factory you call
(`create_sub_socket` vs. `create_xsub_socket`), since the two Protocols' member sets are identical.

---

## `StreamSocket`

Exchanges framed packets directly with raw TCP peers, outside the zlink wire protocol used by
every other socket type.

```python
stream = create_stream_socket(ctx)
stream.on_packet(lambda rid, header, body: ...)
```

**Options.** `stream_options`, `send(routing_id)`, `recv_into(received, *, flags=0)`,
`on_packet(handler)` (the handler owns both header and body messages, running on a background
dispatch thread), `disconnect_rid(peer_rid)`. **No `connect`/`disconnect` declared on this
Protocol** — matching every other language's STREAM asymmetry.

**Completion result.** `recv_into` follows the convention above.

**When to use.** Use `on_packet` for a callback-driven packet loop.

---

## Send / request / reply operation-builder shape

The fluent builder every socket type's `send`/`publish`/`request`/`reply` entry point above returns
to accumulate parts, flags, and a terminal submit. All builder stages extend the shared
`_FluentMessageOp` base Protocol.

```python
dealer.send().message(part1).message(part2).submit()

dealer.request().message(payload).timeout(5.0).submit(
    lambda result, parts: ...
)

received.reply().message(b"ok").submit()
```

**Options.** `_FluentMessageOp` (shared base): `message(payload)` (add one part),
`messages(*payloads)` (add several parts in one call — **declared directly on the shared base
Protocol here**, unlike other languages where the multi-part convenience is a separate extension
method), `flags(flags)`. `SendOp extends _FluentMessageOp` adds `submit()`. `RequestOp extends
_FluentMessageOp` adds `timeout(timeout)` and `submit(callback)` — **callback-only, no
awaitable/Future-returning overload documented on this Protocol**, matching rust's callback-only
request submit rather than dotnet/java/node/cpp's async path. `RequestCallbackOp` mirrors
`RequestOp`'s shape (`timeout`, `submit(callback)`) as a separate Protocol rather than a narrowed
type reached only after calling `.flags(...)` — both `RequestOp` and `RequestCallbackOp` expose
`submit(callback)` directly. `ReplyOp extends _FluentMessageOp` adds `submit()`.

**Completion result.** `SendOp.submit()`/`ReplyOp.submit()` return the operation result
synchronously. `RequestOp`/`RequestCallbackOp.submit(callback)` deliver the reply to `callback`
later, on a background dispatch thread.

**When to use.** Use `messages(*payloads)` to add several parts in one call instead of chaining
`.message(...)` per part. Since there is no async/awaitable request path, bridge to `asyncio`
manually (a `Future`/event set inside the callback) if `await`-style ergonomics are needed at the
call site.

---

## Socket enums

| Enum | Used by | Values |
|---|---|---|
| `SocketType` | Internal socket-kind identification | `ANY`, `PAIR`, `PUB`, `SUB`, `DEALER`, `ROUTER`, `XPUB`, `XSUB`, `STREAM` |
| `SendFlags` | Every send/request/reply builder's `.flags(...)` stage (above) | `NONE`, `DONT_WAIT` |
| `RecvFlags` | Every `recv_into`/`subscribe_into`/`receive_subscription_event_into` | `NONE`, `DONT_WAIT` |
| `SubmitResult` | Mirrored by `SubmitError` (Errors category) | `OK`, `BACKPRESSURED`, `NOT_CONNECTED`, `NOT_FOUND`, `TERMINATED`, `INVALID_HANDLE`, `INVALID_ARGUMENT`, `NOT_SUPPORTED`, `INVALID_STATE`, `THREAD_VIOLATION`, `OUT_OF_MEMORY`, `SEQ_EXHAUSTED`, `INTERNAL_ERROR`, `NOT_ADMITTED` |
| `RequestResult` | Mirrored by `RequestError` (Errors category) | `OK`, `TIMED_OUT`(101), `NOT_FOUND`(102), `TERMINATED`(103), `PROTOCOL_ERROR`(104), `INTERNAL_ERROR`(105), `REJECTED`(106), `CONFLICT`(107), `BUSY`(108), `NOT_CONNECTED`(109), `INVALID_ARGUMENT`(110), `INVALID_STATE`(111), `NOT_SUPPORTED`(112), `BACKPRESSURED`(113) |
| `RecvResult` | Mirrored by `RecvError` (Errors category) | `OK`, `NO_DATA`(201), `BUSY`(202), `TERMINATED`(203), `INVALID_HANDLE`(204), `NOT_SUPPORTED`(205), `INTERNAL_ERROR`(206), `BUFFER_TOO_SMALL`(207), `INVALID_STATE`(208) — the fuller 8-value set (matching node's, not dotnet/cpp/java/rust's 6-value set) |
| `HandlerResult` | Handler registration APIs | `OK`, `INVALID_ARGUMENT`(301), `BUSY`(302), `NOT_SUPPORTED`(303), `DEADLOCK`(304), `INVALID_HANDLE`(305), `INTERNAL_ERROR`(306) |
| `RidDuplicatePolicy` | `CommonSocketOptions.rid_duplicate_policy`, `RouterSocketOptions.handover` | `REJECT`, `HANDOVER` |
| `SubmitRetryMode` | `CommonSocketOptions.submit_retry_mode` | `OFF`, `LOCAL_FAILURE` |

**When to use.** `DONT_WAIT` on either flags enum turns a blocking call into a non-blocking one that
reports `False`/back-pressure instead of blocking.

---

See [`contracts/sockets/`](../../../../bindings/python/src/zlink/contracts/sockets/) and the
[Python binding spec](../../spec/python/README.en.md) for the full rationale.
