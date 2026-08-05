[한국어](02-messaging.ko.md) | English

[Reference index](README.en.md)

# 02. Messaging

This category covers message ownership, the receive envelope types (`received_t`,
`topic_message_t`, `subscription_event_t`), and the shared send/request/reply move-only builder
family every socket type's `send`/`publish`/`request`/`reply` returns. The exact signatures are
owned by [`Contracts/Messaging/`](../../../../bindings/cpp/include/zlink/Contracts/Messaging/).
`lazy_message_parts.hpp` and `operation_builder_base.hpp` are under `namespace zlink::detail` and
have no public contract entry.

---

## `message_t`

Owns one zlink message payload — the unit every send, request, reply, and receive API moves.

```cpp
zlink::message_t empty;
zlink::message_t sized (4096);
zlink::message_t copy = zlink::message_t::from (std::string ("payload"));
```

**Options.** Constructors: `message_t()` (empty), `explicit message_t(size_t size_)` (writable
storage). Static factories: `allocate(size_t)`, `from(const std::vector<uint8_t>&)`,
`from(std::span<const std::byte>)`, `from(std::span<const uint8_t>)`, `from(const std::string&)`
(UTF-8 encode). Template factories/parsers exist for JSON/MessagePack/protobuf
(`from_json`/`from_messagepack`/`from_protobuf`, `parse_json`/`parse_messagepack`/`parse_protobuf`)
— a codec helper itself is not part of the C++ binding package; these delegate to a framework
codec extension. Instance members: `data()`/`bytes()` (mutable and `const` overloads), `size()`,
`is_empty()`, `ref_count()`, `to_bytes()`, `copy_to(std::span<std::byte>)`/
`copy_to(std::span<uint8_t>)`, `to_string()`, `close()`. Copy construction/assignment perform a
deep copy of the payload.

**Completion result.** Every member is synchronous. Sending a message consumes its payload — the
native frame moves into the transport on a successful send, leaving the instance invalid; call
`close()` to release a message that will not be sent. A pointer/span returned by `data()`/`bytes()`
is valid only while the message remains valid (not sent or closed).

**When to use.** Use a sized constructor or a copying `from(...)` factory to build an outbound
payload from data the caller doesn't need to keep raw ownership of. Use
`zlink::advanced::external_message_t::from(span, free_fn, hint)` (the advanced no-copy overload,
declared alongside `message_t`) only when a caller-owned buffer must be handed to a message without
a copy — it entrusts the buffer to the message, which calls `free_fn(data, hint)` exactly once when
released.

---

## `received_t`

Holds one received message envelope: routing metadata, parts, and an optional reply context.

```cpp
zlink::received_t received;
if (dealer.recv (received) == 0) { /* ... */ }
if (received.request_seq ()) {
    received.reply ().message (reply_msg).submit ();
}
```

**Options.** Default-constructible; copyable and movable. Read-only accessors: `routing_id()`
(`const std::optional<routing_id_t>&`), `request_seq()` (`const std::optional<uint64_t>&`),
`parts()` (`const std::vector<message_t>&`/mutable overload), `is_single_part()`. Methods:
`first_part()`, `single_part_or_throw()`, `send()` (starts the shared `send_operation_t` builder,
addressed to this envelope's captured routing id), `reply()` (starts the shared `reply_operation_t`
builder — throws on `submit()` if there is no valid reply context), `close()`.

**Completion result.** All members are synchronous. `send()`/`reply()` reconstruct the send/reply
context lazily at submit time from the stored routing id and request sequence — this avoids a
per-receive `std::function` closure and its heap allocation on the server hot path.

**When to use.** Reuse one `received_t` across a receive loop rather than constructing a new one
per message. Check `request_seq()` before calling `reply()` to confirm the envelope is actually
replyable.

---

## `topic_message_t`

Holds one received publish: its topic and message parts.

```cpp
zlink::topic_message_t published;
if (sub.subscribe (published) == 0) {
    const std::string &topic = published.topic ();
}
```

**Options.** Default-constructible, plus a constructor taking routing id/topic/parts directly.
Read-only accessors: `routing_id()` (`const std::optional<routing_id_t>&`), `topic()` (`const
std::string&`), `parts()`, `is_single_part()`, `first_part()`, `single_part_or_throw()`, `close()`.

**Completion result.** Synchronous.

**When to use.** Reuse one instance across a subscribe-receive loop the same way as `received_t`.

---

## `subscription_event_t` / `subscription_filter_t`

Reports one subscriber's subscribe or unsubscribe (as observed by an XPUB socket), and describes
one active subscription entry.

```cpp
zlink::subscription_event_t evt;
if (xpub.receive_subscription_event (evt) == 0) { /* ... */ }
```

**Options.** `subscription_event_t` is a plain struct: `routing_id` (`std::optional<routing_id_t>`),
`topic` (`std::string`), `subscribed` (`bool`). `subscription_filter_t`: `filter` (`std::string`),
`is_pattern` (`bool`, defaults `false`).

**Completion result.** Both are plain data structs with no disposal or async behavior.

**When to use.** Use on an XPUB socket's subscription-event receive path (Sockets category) to
observe subscriber churn. Use `subscription_filter_t` as the return type of a socket's
`subscription_at(index)` overload that returns by value.

---

## Send / request / reply operation-builder shape

The move-only fluent builder every `send`, routed `send`, `publish`, `request`, and `reply` entry
point (all in the Sockets category) returns to accumulate parts, flags, and a terminal submit.
Every builder in this family is move-only and inherits privately from a shared
`detail::operation_builder_base_t` — not part of the public contract itself.

```cpp
std::move (dealer.send ()).message (part1).message (part2).submit ();

auto result = std::move (dealer.request ())
    .message (request_msg)
    .timeout (std::chrono::seconds (5))
    .async ();
std::vector<zlink::message_t> reply = result.get ();

std::move (received.reply ()).message (reply_msg).submit ();
```

**Options.** `send_operation_t::message(message_t&)`/`message(message_t&&)` (ref-qualified `&&`,
so the builder is consumed by each call — chain with `std::move(...)`) starts the chain, returning
`send_submit_operation_t`, whose `.message(...)`/`.flags(int)`/`.submit()` accumulate further parts,
set flags, and terminate. `request_operation_t`/`request_submit_operation_t` mirror this shape plus
`.timeout(std::chrono::milliseconds)`; calling `.flags(int)` on `request_submit_operation_t`
narrows the builder to `request_callback_submit_operation_t`, dropping the awaitable `.async()`
path — only `.submit(request_callback_t)` remains reachable past that point. `reply_operation_t`/
`reply_submit_operation_t` mirror `send_operation_t`/`send_submit_operation_t` but their `.flags(...)`
call throws `submit_error_t{not_supported}` if given anything but `send_flags_t::none` — the core
reply function takes no send-flag argument.

**Completion result.** `send_submit_operation_t::submit()`/`reply_submit_operation_t::submit()` are
synchronous; send's returns `bool` (`false` only when `send_flags_t::dontwait` was set and the send
would have blocked — other failures throw `submit_error_t`), reply's returns `void`.
`request_submit_operation_t::async()` returns `async_result_t<std::vector<message_t>>` — call
`.get()` to block for the reply (owned by the caller), or `.wait_for(...)`/`.wait_until(...)` to
poll with a timeout; the result type internally pumps request progress while waiting rather than
blocking the OS thread outright. `request_submit_operation_t`/
`request_callback_submit_operation_t::submit(request_callback_t)` returns `bool` (same `dontwait`
convention) and delivers `(request_result_t, std::vector<message_t>)` later — the vector is
populated only when the result is `request_result_t::ok`, and the callback owns and must `close()`
each message. Every builder consumes its accumulated `message_t` parts on a successful submit only;
on failure ownership is restored to the caller.

**When to use.** Use `.async()` when the caller can wait on a future/`async_result_t`; use
`.flags(...).submit(callback)` for callback-driven completion instead. Use `received_t::reply()`/
`send()` rather than reconstructing the destination by hand. Since `message()` overloads are
`&&`-qualified, always chain from an rvalue (`std::move(socket.send())...` or the direct chain
shown above) — an lvalue builder cannot call `.message(...)` directly.

---

See [`Contracts/Messaging/`](../../../../bindings/cpp/include/zlink/Contracts/Messaging/) and the
[C++ binding spec](../../spec/cpp/README.en.md) for the full rationale.
