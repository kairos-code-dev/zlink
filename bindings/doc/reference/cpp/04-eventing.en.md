[한국어](04-eventing.ko.md) | English

[Reference index](README.en.md)

# 04. Eventing

This category covers socket monitoring, the reusable poller, and standalone timers — created via
`socket_t::monitor_open(...)` (Sockets category) and by direct construction (`poller_t`,
`timer_t`) respectively; there is no `Zlink.CreatePoller()`/`CreateTimer()`-style factory in this
projection. The exact signatures are owned by
[`Contracts/Eventing/`](../../../../bindings/cpp/include/zlink/Contracts/Eventing/).

---

## `socket_monitor_t`

Observes a socket's connection lifecycle events and reads its current status.

```cpp
zlink::socket_monitor_t monitor =
    zlink::socket_monitor_t::open (socket, zlink::monitor_event::connected | zlink::monitor_event::disconnected);
monitor.on_event ([] (const zlink::monitor_event_t &e) { /* ... */ });
zlink::monitor_status_t status = monitor.status ();
```

**Options.** `socket_monitor_t()` (default, invalid until assigned), static `open(const socket_t&,
monitor_event = monitor_event::all)` (the actual constructor path — matches
`socket_t::monitor_open(...)`, which calls this internally). `valid()`, `on_event(std::function<void
(const monitor_event_t&)>)`, `recv(recv_flags_t = recv_flags_t::none)` (returns
`std::optional<monitor_event_t>`), `status() const` (returns `monitor_status_t`), `close()`. A
static no-op `ignore_event(const monitor_event_t&) noexcept` is also declared, for a caller that
wants to register a handler that intentionally does nothing.

**Completion result.** All members are synchronous. Move-only; the destructor does not implicitly
close — call `close()` explicitly.

**When to use.** Use `on_event` for a passive lifecycle observer registered once; use `recv` for a
pull-based drain loop instead. Use `status()` for a point-in-time snapshot.

---

## `monitor_status_t`

A snapshot of a socket's monitored state and auto-high-water-mark telemetry, returned by
`socket_monitor_t::status()`. A plain struct (not a class with accessor methods, unlike dotnet's
`MonitorStatus`) — every field is public data, default-initialized to zero/false in its default
constructor.

**Options.** No parameters — construct via `status()`, not directly.

| Group | Fields |
|---|---|
| ABI identity | `abi_version`, `struct_size` (`uint32_t`) |
| Source/state | `source_kind` (`monitor_source_kind`), `state_flags`/`detail_flags` (`uint32_t` bitmasks — see enums below), `is_ready()` (computed method: `(state_flags & 1) != 0`) |
| Pending counts | `snd_pending_msgs`, `rcv_pending_msgs` (`uint64_t`) |
| Auto-HWM config | `auto_hwm_enabled` (`bool`), `auto_hwm_profile`, `auto_hwm_role`, `auto_hwm_policy_class` (`uint32_t` — not the `zlink::auto_hwm_profile` enum type; a raw integer field here), `auto_hwm_unit_budget_bytes`, `auto_hwm_socket_message_slots` (`uint64_t`), `auto_hwm_size_cap` (`uint32_t`) |
| Connection bucket | `auto_hwm_connection_bucket_enabled` (`bool`), `auto_hwm_connection_bucket_count`/`_index`/`_hwm_4k` (`uint32_t`), `auto_hwm_connection_bucket_hysteresis_retained` (`bool`) |
| Auto-HWM plan (bytes) | `auto_hwm_effective_message_bytes`, `auto_hwm_planned_sndhwm_bytes`/`_rcvhwm_bytes`, `auto_hwm_applied_sndhwm_bytes`/`_rcvhwm_bytes` (`uint64_t`), `auto_hwm_effective_sndbuf`/`_rcvbuf` (`int32_t`) |
| Auto-HWM recalc | `auto_hwm_last_recalc_ms` (`uint64_t`), `auto_hwm_last_recalc_reason` (`uint32_t`), `auto_hwm_send_blocked_ratio_ppm` (`uint32_t`) |
| Auto-HWM deferred shrink | `auto_hwm_deferred_sndhwm_bytes`/`_rcvhwm_bytes` (`uint64_t`, valid only when the matching `auto_hwm_deferred_sndhwm_valid`/`_rcvhwm_valid` `bool` is true) |
| In-flight/charging | `snd_bytes_in_flight`, `rcv_bytes_in_flight`, `minimum_core_message_charge_bytes`, `oversize_message_admission_count`, `oversize_message_admission_max_bytes` (`uint64_t`) |

**Completion result.** N/A — plain data, no disposal.

**When to use.** Read `is_ready()` instead of decoding `state_flags` directly. Use the
connection-bucket and auto-HWM-plan fields when diagnosing why a socket's effective send/receive
HWM differs from its configured `common_socket_options_t` value (Sockets category).

---

## `poller_t`

Multiplexes sockets, file descriptors, monitors, and timers on a single reusable wait.

```cpp
zlink::poller_t poller;
poller.add (dealer, zlink::poll_event_flag_t::pollin, /*slot=*/1);
poller.add (timer, /*slot=*/2);
std::vector<zlink::poll_event_t> ready (8);
size_t count = poller.wait (ready.data (), ready.size (), std::chrono::seconds (1));
```

**Options.** `add(socket_monitor_t&, poll_event_flag_t, std::uintptr_t slot_)`, `add(socket_t&,
poll_event_flag_t, std::uintptr_t slot_)`, `add_fd(int fd_, poll_event_flag_t, std::uintptr_t
slot_)`, `add(timer_t&, std::uintptr_t slot_)` — `slot_` is a caller token echoed back in the
matching `poll_event_t`. `modify_fd(int, poll_event_flag_t)`, `modify(socket_monitor_t&,
poll_event_flag_t)`, `modify(socket_t&, poll_event_flag_t)`. `remove(socket_monitor_t&)`/
`remove(socket_t&)`/`remove(timer_t&)`/`remove_fd(int)` (each returns `bool`, true when it was
registered). `size() const` (`int`). `close()`. `wait(poll_event_t* events_, size_t capacity_,
std::chrono::milliseconds timeout_)`.

**Completion result.** Registration/removal members are synchronous. `wait` blocks up to `timeout_`,
writing up to `capacity_` results and returning the count written as `size_t` (`0` on timeout).
Note `poller_t` can also register a `socket_monitor_t&` directly (unlike dotnet's `IPoller`, whose
`Add` overloads take `IZlinkSocket`/`IZlinkTimer` only — a monitor is polled there indirectly
through `ZlinkPoll.Poll(IReadOnlyList<ISocketMonitor>, ...)` instead).

**When to use.** Use one poller across a service's lifetime. Prefer `modify` over `remove` + `add`
when only the watched events change.

---

## `poll_event_t`

One ready source reported by a `poller_t::wait` call. A plain struct, default-constructed with
`source_kind = poll_source_kind_t::socket`.

**Options.** No parameters. Fields: `source_kind` (`poll_source_kind_t`: `socket`/`fd`/`timer`),
`slot` (`std::uintptr_t`, the caller token supplied at registration), `revents`
(`poll_event_flag_t`, the events that actually fired), `fd` (`int`, populated for `fd`-kind
sources).

**Completion result.** N/A — plain data.

**When to use.** Branch on `source_kind`/`slot` to route each `wait` result back to the socket,
descriptor, or timer it corresponds to.

---

## `poll_item_t` / `zlink::poll(...)`

A standalone one-shot poll helper distinct from `poller_t` — builds a plain array of watch items
and waits once, without registering anything persistently.

```cpp
std::vector<zlink::poll_item_t> items {
    zlink::poll_item_t::from_socket (dealer, zlink::poll_event_flag_t::pollin),
    zlink::poll_item_t::from_fd (raw_fd, zlink::poll_event_flag_t::pollin),
};
int ready = zlink::poll (items, std::chrono::milliseconds (1000));
```

**Options.** `poll_item_t`: `socket` (`socket_t*`, null for an fd-based item), `fd` (`int`),
`events`/`revents` (`poll_event_flag_t`); static factories `from_socket(socket_t&,
poll_event_flag_t)`/`from_fd(int, poll_event_flag_t)`. `zlink::poll(poll_item_t* items_, size_t
count_, std::chrono::milliseconds timeout_)` and the `std::vector<poll_item_t>&` convenience
overload.

**Completion result.** Synchronous; returns the count of ready items (`0` on timeout). `revents`
on each `poll_item_t` is written in place by the call.

**When to use.** Use this free-function form for an ad hoc, one-off wait across a small fixed set
of sockets/descriptors; use `poller_t` instead when the watched set changes over time or a monitor/
timer needs to be multiplexed alongside sockets.

---

## `timer_t`

A standalone timer that fires on an interval and can be polled (via `recv`) or driven through a
poller.

```cpp
zlink::timer_t timer;
timer.on_fire ([] (uint64_t count) { /* ... */ });
timer.start (std::chrono::seconds (1), /*repeat_count=*/0);
```

**Options.** `start(duration, uint64_t repeat_count_ = 0)` (a template accepting any
`std::chrono::duration`, converted internally to nanoseconds; a negative interval throws
`config_error_t{invalid_argument}`), `stop()`, `recv()` (returns `std::optional<uint64_t>` — the
cumulative fire count, `std::nullopt` when nothing pending), `on_fire(std::function<void
(uint64_t)>)` (unlike dotnet's `Action<IZlinkTimer, ulong>`, the callback here receives only the
fire count, not the timer itself), `close()`.

**Completion result.** All members are synchronous. Move-only; the destructor does not implicitly
close.

**When to use.** Use `on_fire` for a passive interval callback; use `recv` to poll expirations
instead, or register the timer with `poller_t::add(timer_t&, std::uintptr_t)` to multiplex it
alongside sockets on one wait.

---

## Eventing enums

Shared enums referenced across every entry above.

| Enum | Used by | Values |
|---|---|---|
| `monitor_event` | `socket_monitor_t::open`/`monitor_event_t::event` | `connected`, `connect_delayed`, `connect_retried`, `listening`, `bind_failed`, `accepted`, `accept_failed`, `closed`, `close_failed`, `disconnected`, `monitor_stopped`, `handshake_failed_no_detail`, `connection_ready`, `handshake_failed_protocol`, `handshake_failed_auth`, `peer_weight_changed`, `all` |
| `monitor_target_kind_t` | Not reached through any entry documented in this category | `socket`, `discovery`, `spot` — the latter two have no corresponding public monitor-creation entry point in this reference tier |
| `monitor_source_kind` | `monitor_status_t::source_kind` | `socket`(1), `spot_pub`(3), `spot_sub`(4) — the latter two are declared but, like `monitor_target_kind_t::spot`, have no reachable public source in the bindings layer (SPOT/Actor lives only at the framework layer) |
| `monitor_state` | `monitor_status_t::state_flags` (bitmask) | `ready`(1), `bound_ready`(2), `closed`(8) |
| `monitor_status_detail` | `monitor_status_t::detail_flags` (bitmask) | `snd_pending_msgs`(2), `rcv_pending_msgs`(4) |
| `disconnect_reason` | Not reached through any entry documented in this category | `unknown`, `handshake_failed`, `transport_error`, `ctx_term` |
| `poll_source_kind_t` | `poll_event_t::source_kind` | `socket`, `fd`, `timer` |
| `poll_event_flag_t` | `poller_t::add`/`modify`/`wait`, `poll_item_t`, `zlink::poll` | `none`, `pollin`, `pollout`, `pollerr`, `pollcompletion` (no `pollpri` in this projection, unlike dotnet's `PollEventFlags.PollPri`) |

**When to use.** Treat `monitor_source_kind::spot_pub`/`spot_sub`, `monitor_target_kind_t::spot`/
`discovery`, and `disconnect_reason` as declared-but-currently-unreachable from this bindings
layer's public entry points — a spec-level question, not something to route around in application
code today.

---

See [`Contracts/Eventing/`](../../../../bindings/cpp/include/zlink/Contracts/Eventing/) and the
[C++ binding spec](../../spec/cpp/README.en.md) for the full rationale.
