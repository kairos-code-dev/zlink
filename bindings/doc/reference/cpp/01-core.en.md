[한국어](01-core.ko.md) | English

[Reference index](README.en.md)

# 01. Core

This category covers the context lifecycle, context options, routing identity, and free
utility/capability functions. Socket creation happens via each concrete socket type's own
constructor (Sockets category), not a factory method here — unlike dotnet's `IContext.CreateXxx()`
methods. The exact signatures are owned by
[`Contracts/Core/`](../../../../bindings/cpp/include/zlink/Contracts/Core/).

---

## `context_t`

A messaging context: the factory and owner of sockets, and the prerequisite for constructing any
socket type.

```cpp
zlink::context_t ctx;
zlink::context_t ctx_with_threads (zlink::io_thread_count_t::value (4));
```

**Options.**

| Member | Meaning |
| --- | --- |
| `context_t()` / `explicit context_t(io_thread_count_t)` | default / with I/O thread count |
| move constructible/assignable; copy deleted | — |
| `valid()` | `bool` |
| `shutdown()` | interrupts blocking operations on sockets under this context without closing them |
| `term()` | terminates and destroys the context |
| `options()` | returns `context_options_t`, below |
| `recalculate_auto_hwm()` | — |

**Completion result.** All synchronous, no return value except `valid()`/`options()`. The
destructor calls `term()` if not already terminated.

**When to use.** One `context_t` per context the application needs; most applications need exactly
one. Call `shutdown()` before destruction when sockets are in use across multiple threads.

---

## `context_options_t`

The typed options facade reached via `ctx.options()`.

```cpp
ctx.options ().io_threads (zlink::io_thread_count_t::value (8));
ctx.options ().auto_hwm_profile (zlink::auto_hwm_profile::low_latency);
ctx.options ().add_thread_affinity (zlink::cpu_index_t::value (2));
```

**Options.** Paired getter/setter methods (getter has no suffix, setter takes the new value):

| Member | Type | Meaning |
| --- | --- | --- |
| `io_threads()` | `io_thread_count_t` | — |
| `max_sockets()` | `socket_count_t` | — |
| `max_msg_size()` | `byte_size_t` | — |
| `thread_priority()` | `std::optional<thread_priority_t>` | — |
| `thread_scheduling_policy()` | `thread_scheduling_policy_t` | — |
| `thread_name_prefix()` | `std::string` | — |
| `blocky()` | `bool` | — |
| `auto_hwm_enabled()` | `bool` | — |
| `auto_hwm_recalc_debounce()` | `std::chrono::milliseconds` | — |
| `auto_hwm_profile()` | `zlink::auto_hwm_profile` | — |
| `auto_hwm_msg_unit_bytes()` | `byte_count_t` | — |
| `socket_limit()` | `socket_count_t` | read-only |
| `msg_t_size()` | `byte_size_t` | read-only |
| `add_thread_affinity(cpu_index_t)` / `remove_thread_affinity(cpu_index_t)` | — | setter-only |

**Completion result.** Every getter/setter is synchronous.

**When to use.** Adjust before constructing sockets when the defaults don't fit the deployment.
Pair an `auto_hwm_profile`/`auto_hwm_enabled` change with `context_t::recalculate_auto_hwm()` to
apply it immediately.

---

## Strongly-typed option value wrappers

Small value-type wrappers used throughout `context_options_t` and socket options instead of raw
`int`/`uint32_t`, each constructed via a static `value(...)` factory.

**Options.**

| Type | Wraps | Meaning |
| --- | --- | --- |
| `io_thread_count_t`, `socket_count_t`, `worker_count_t`, `thread_priority_t`, `cpu_index_t`, `socket_backlog_t` | `int` via `::value(int)`/`.value()` | — |
| `byte_size_t` | `int64_t` via `::bytes(int64_t)`/`.bytes()` | — |
| `byte_count_t` (Core) | `uint64_t` via `::bytes(uint64_t)`/`.bytes()` | lossless byte count used by HWM and byte-budget options |
| `peer_weight_t` | `uint32_t` via `::value(uint32_t)` | throws `std::invalid_argument` outside 0-100 |

**Completion result.** All factories and accessors are `noexcept` except `peer_weight_t::value`,
which validates its range.

**When to use.** Construct these at the call site (`io_thread_count_t::value(4)`) rather than
passing a bare integer — the wrapper exists specifically so a mismatched unit doesn't compile.

---

## `routing_id_t`

A binary-safe value type identifying a messaging peer or route, 1 to 255 bytes.

```cpp
auto from_string = zlink::routing_id_t::from (std::string ("worker-3"));
auto from_bytes = zlink::routing_id_t::from (raw_bytes);
auto from_uint = zlink::routing_id_t::from (uint32_t{42});
auto restored = zlink::routing_id_t::from_hex (previously_printed.to_hex ());
```

**Options.**

| Member | Meaning |
| --- | --- |
| `routing_id_t(const uint8_t *bytes_, size_t size_)` | constructor |
| `from(const uint8_t*, size_t)` / `from(const std::vector<uint8_t>&)` | raw bytes as-is |
| `from(const std::string&)` | raw bytes, not UTF-8-validated |
| `from(uint32_t)` | 4-byte big-endian |
| `from(const std::array<uint8_t, 16>&)` | 16-byte value, e.g. a GUID's raw bytes |
| `from_hex(const std::string&)` | restore bytes `to_hex()` printed |
| `data()` / `size()` / `to_bytes()` | — |
| `to_string()` | printable UTF-8, then 4-byte-as-uint32, then 16-byte-as-GUID, then `hex:`-prefixed fallback |
| `to_hex()` | round-trippable with `from_hex` |
| `operator==`/`!=`, `std::hash<routing_id_t>` | value equality, unordered-container support |

**Completion result.** Every factory and accessor is synchronous. Empty input, input over 255
bytes, or a null pointer with nonzero size throws `std::invalid_argument`; a malformed hex string
to `from_hex` throws the same.

**When to use.** `from(const std::string&)` for a human-assigned identity, `from(uint32_t)`/the
16-byte array overload for a numeric or GUID-shaped identity, raw byte overloads when the identity
is already binary. `to_hex()`/`from_hex()` for a durable round trip — `to_string()` is
display-only.

---

## `zlink::version` / `zlink::error_text` / `zlink::has`

Reads the native library's build version, converts a native error code to a message, or checks for
an optional build capability.

```cpp
int major, minor, patch;
zlink::version (major, minor, patch);
const char *message = zlink::error_text (errnum);
bool has_tls = zlink::has ("tls");
```

**Options.**

| Member | Parameters | Meaning |
| --- | --- | --- |
| `version(int &major_, int &minor_, int &patch_)` | three output references | — |
| `error_text(int errnum_) noexcept` | — | returns `const char*`; caller must not modify or free it |
| `has(const std::string &capability_)` | `"tcp"`/`"ipc"`/`"tls"`/`"ws"`/`"wss"`; unrecognized returns `false` | — |

**Completion result.** All three are synchronous, non-throwing.

**When to use.** `version()` to confirm a dynamically-loaded native library matches expectations.
`has(...)` at startup to branch on optional transports.

---

## `stopwatch_t` / `atomic_counter_t` / `thread_t`

A high-resolution stopwatch, a thread-safe integer counter, and a running background thread —
three independent utility resources with the same RAII shape.

```cpp
zlink::stopwatch_t watch;
uint64_t partial_us = watch.intermediate ();
uint64_t total_us = watch.stop ();

zlink::atomic_counter_t counter;
int new_value = counter.increment ();

zlink::thread_t worker ([] { do_work (); });
worker.join ();
```

**Options.** All three: default-constructible, move-only, `valid() const noexcept`, `close()`.

| Type | Members |
| --- | --- |
| `stopwatch_t` | `intermediate()`/`stop()` (both `uint64_t` microseconds) |
| `atomic_counter_t` | `set(int)`, `increment()`/`decrement()` (return the *new* value), `value() const` |
| `thread_t(std::function<void()> task_)` | runs the task immediately on construction; `join()` blocks until it finishes |

**Completion result.** All synchronous; the destructor calls `close()` if not already closed.

**When to use.** `atomic_counter_t` for a shared count safe across threads. `stopwatch_t` for
benchmarking. `thread_t` for a portable background thread instead of a platform-specific API.

---

See [`Contracts/Core/`](../../../../bindings/cpp/include/zlink/Contracts/Core/) and the
[C++ binding spec](../../spec/cpp/README.en.md) for the full rationale.
