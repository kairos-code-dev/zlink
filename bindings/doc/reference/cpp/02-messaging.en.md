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

**Options.**

| Member | Meaning |
| --- | --- |
| `message_t()` | empty |
| `explicit message_t(size_t size_)` | writable storage |
| `allocate(size_t)` | static factory |
| `from(const std::vector<uint8_t>&)` / `from(std::span<const std::byte>)` / `from(std::span<const uint8_t>)` | static factories, copy |
| `from(const std::string&)` | UTF-8 encode |
| `from_json`/`from_messagepack`/`from_protobuf`, `parse_json`/`parse_messagepack`/`parse_protobuf` | template factories/parsers; delegate to a framework codec extension, not part of this package |
| `data()`/`bytes()` | mutable and `const` overloads |
| `size()` / `is_empty()` / `ref_count()` | — |
| `to_bytes()` / `copy_to(std::span<std::byte>)` / `copy_to(std::span<uint8_t>)` / `to_string()` | — |
| `close()` | — |

Copy construction/assignment perform a deep copy of the payload.

**Completion result.** Every member is synchronous. Sending a message consumes its payload — the
native frame moves into the transport on a successful send, leaving the instance invalid; call
`close()` to release a message that will not be sent.

**When to use.** A sized constructor or a copying `from(...)` factory to build an outbound payload
from data the caller doesn't need to keep raw ownership of. Use
`zlink::advanced::external_message_t::from(span, free_fn, hint)` (a no-copy overload declared
alongside `message_t`) only when a caller-owned buffer must be handed to a message without a copy.

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

**Options.** Default-constructible; copyable and movable.

| Member | Returns | Meaning |
| --- | --- | --- |
| `routing_id()` | `const std::optional<routing_id_t>&` | — |
| `request_seq()` | `const std::optional<uint64_t>&` | — |
| `parts()` | `const std::vector<message_t>&` (mutable overload too) | — |
| `is_single_part()` | `bool` | — |
| `first_part()` / `single_part_or_throw()` | `message_t` | — |
| `send()` | builder | starts the shared `send_operation_t`, addressed to this envelope's captured routing id |
| `reply()` | builder | starts the shared `reply_operation_t`; throws on `submit()` if there is no valid reply context |
| `close()` | — | — |

**Completion result.** All synchronous. `send()`/`reply()` reconstruct the send/reply context
lazily at submit time from the stored routing id and request sequence, avoiding a per-receive
`std::function` closure and heap allocation on the server hot path.

**When to use.** Reuse one `received_t` across a receive loop. Check `request_seq()` before calling
`reply()` to confirm the envelope is actually replyable.

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

| Member | Returns | Meaning |
| --- | --- | --- |
| `routing_id()` | `const std::optional<routing_id_t>&` | — |
| `topic()` | `const std::string&` | — |
| `parts()` / `is_single_part()` / `first_part()` / `single_part_or_throw()` / `close()` | — | same shape as `received_t` |

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

**Options.**

| Type | Field | Meaning |
| --- | --- | --- |
| `subscription_event_t` | `routing_id` (`std::optional<routing_id_t>`) | — |
| | `topic` (`std::string`) | — |
| | `subscribed` (`bool`) | — |
| `subscription_filter_t` | `filter` (`std::string`) | — |
| | `is_pattern` (`bool`, default `false`) | — |

**Completion result.** Both are plain data structs with no disposal or async behavior.

**When to use.** On an XPUB socket's subscription-event receive path (Sockets category) to observe
subscriber churn. `subscription_filter_t` as the return type of a socket's `subscription_at(index)`
overload.

---

## Send / request / reply operation-builder shape

The move-only fluent builder every `send`, routed `send`, `publish`, `request`, and `reply` entry
point (all in the Sockets category) returns to accumulate parts, flags, and a terminal submit.
Every builder in this family inherits privately from a shared
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

**Options.**

| Stage | Member | Meaning |
| --- | --- | --- |
| `send_operation_t` | `.message(message_t&)`/`.message(message_t&&)` | `&&`-qualified — builder is consumed by each call, chain with `std::move(...)` |
| `send_submit_operation_t` | `.message(...)` / `.flags(int)` / `.submit()` | add parts, set flags, terminal |
| `request_operation_t`/`request_submit_operation_t` | same as `send` + `.timeout(std::chrono::milliseconds)` | — |
| `request_submit_operation_t.flags(int)` | narrows to `request_callback_submit_operation_t` | drops the awaitable `.async()` path — only `.submit(request_callback_t)` remains |
| `reply_operation_t`/`reply_submit_operation_t` | mirrors `send`, `.flags(...)` throws `submit_error_t{not_supported}` for anything but `send_flags_t::none` | core reply function takes no send-flag argument |

**Completion result.** `send_submit_operation_t::submit()`/`reply_submit_operation_t::submit()` are
synchronous — send returns `bool` (`false` only under `send_flags_t::dontwait` back-pressure;
other failures throw `submit_error_t`), reply returns `void`. `request_submit_operation_t::async()`
returns `async_result_t<std::vector<message_t>>` — `.get()` blocks for the reply, or
`.wait_for(...)`/`.wait_until(...)` polls with a timeout (internally pumps request progress rather
than blocking the OS thread). `submit(request_callback_t)` (on `request_submit_operation_t`/
`request_callback_submit_operation_t`) returns `bool` and delivers `(request_result_t,
std::vector<message_t>)` later — the vector is populated only when the result is
`request_result_t::ok`, and the callback owns and must `close()` each message. Every builder
consumes its parts on a successful submit only.

**When to use.** `.async()` when the caller can wait on a future; `.flags(...).submit(callback)`
for callback-driven completion instead. `received_t::reply()`/`send()` rather than reconstructing
the destination by hand. Since `message()` overloads are `&&`-qualified, always chain from an
rvalue — an lvalue builder cannot call `.message(...)` directly.

---

See [`Contracts/Messaging/`](../../../../bindings/cpp/include/zlink/Contracts/Messaging/) and the
[C++ binding spec](../../spec/cpp/README.en.md) for the full rationale.
