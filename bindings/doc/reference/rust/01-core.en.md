[한국어](01-core.ko.md) | English

[Reference index](README.en.md)

# 01. Core

This category covers the context lifecycle, context options, routing identity, and the crate-root
free functions. **Unlike every other wrapper binding covered so far, these free functions are not
declared under `contracts/core/`** — they live at the crate root in
[`src/lib.rs`](../../../../bindings/rust/src/lib.rs). `Stopwatch`/`AtomicCounter`/`Thread` are
declared in `contracts/core/utilities.rs`. The exact signatures are owned by
[`contracts/core/`](../../../../bindings/rust/src/contracts/core/) and
[`src/lib.rs`](../../../../bindings/rust/src/lib.rs).

---

## `Context::new()`

Creates a messaging context — the factory and owner of sockets.

```rust
let ctx = Context::new()?;
```

**Options.** No parameters.

**Completion result.** Returns `Result<Context, ConfigError>` — **context creation itself is
fallible here**, unlike every other language covered so far, where the equivalent factory has no
error path in its public signature. `Context` is `Send`/`Sync` and may be shared across threads
(for example via `std::sync::Arc`); the context terminates when the last owning `Context` value is
dropped, so an owner must stay alive while another thread creates or uses sockets from it.

**When to use.** Call once per context the application needs; most applications need exactly one.
Keep an owning `Context` (or an `Arc<Context>`) alive for as long as any thread is creating or
using sockets from it.

---

## `Context::shutdown()` / `Context::recalculate_auto_hwm()`

Interrupts blocking operations on the context's sockets without closing them, or forces an
immediate recalculation of automatic high-water marks.

```rust
ctx.shutdown()?;
ctx.recalculate_auto_hwm()?;
```

**Options.** Neither takes parameters.

**Completion result.** `shutdown()` returns `Result<(), CloseError>`; `recalculate_auto_hwm()`
returns `Result<(), ConfigError>`. `shutdown` interrupts blocking calls on sockets under this
context but does not drop the context or its sockets. `recalculate_auto_hwm` recomputes automatic
HWM only for sockets still configured with an `AutoHwmProfile`.

**When to use.** Call `shutdown()` before dropping a context with sockets in use across multiple
threads, to avoid a thread blocking on a socket call indefinitely. Call `recalculate_auto_hwm()`
after changing the auto-HWM profile or a message-unit option, to apply new sizing immediately.

---

## `Context::options()`

Reads the context-wide options facade, whose properties govern I/O threads and the defaults every
socket created from the context inherits.

```rust
let options = ctx.options();
options.set_io_threads(8)?;
options.set_auto_hwm_profile(AutoHwmProfile::LowLatency)?;
options.add_thread_affinity(2)?;
```

**Options.** Paired getter/setter methods, every one returning `Result<T, ConfigError>`:
`io_threads()`/`set_io_threads(i32)`, `max_sockets()`/`set_max_sockets(i32)`, `socket_limit()`
(read-only), `thread_priority()`/`set_thread_priority(i32)`,
`thread_scheduling_policy()`/`set_thread_scheduling_policy(i32)`,
`max_message_size()`/`set_max_message_size(i32)`, `msg_t_size()` (read-only),
`blocky()`/`set_blocky(bool)`, `thread_name_prefix()`/`set_thread_name_prefix(&str)`,
`auto_hwm_enabled()`/`set_auto_hwm_enabled(bool)`,
`auto_hwm_recalc_debounce()`/`set_auto_hwm_recalc_debounce(Duration)`,
`auto_hwm_profile()`/`set_auto_hwm_profile(AutoHwmProfile)`,
`auto_hwm_msg_unit_bytes()`/`set_auto_hwm_msg_unit_bytes(u64)`. Setter-only:
`add_thread_affinity(i32)`/`remove_thread_affinity(i32)`.

**Completion result.** Every getter/setter is synchronous, returning `Result<_, ConfigError>` (every
option access can fail, unlike languages where the getter/setter throws only on a genuinely
exceptional error).

**When to use.** Adjust before creating sockets when the defaults don't fit the deployment. Pair an
`auto_hwm_profile`/`auto_hwm_enabled` change with `Context::recalculate_auto_hwm()` to apply it
immediately.

---

## `Context::pair_socket()` / `dealer_socket()` / `router_socket()` / `pub_socket()` / `sub_socket()` / `xpub_socket()` / `xsub_socket()` / `stream_socket()`

Creates a socket of the given type from a context, owned by the caller.

```rust
let dealer = ctx.dealer_socket()?;
```

**Options.** None of the eight factory methods takes parameters.

**Completion result.** Each returns `Result<SocketType, ConfigError>` — **socket creation itself is
fallible**, unlike dotnet/java/node/cpp, where the equivalent factory has no error path in its
public signature.

**When to use.** See the Sockets category for each socket type's operations, options, and
capability roles — this entry only covers how each is created.

---

## `RoutingId`

A binary-safe value type identifying a messaging peer or route, 1 to 255 bytes.

```rust
let from_string: RoutingId = "worker-3".into();
let from_bytes: RoutingId = raw_bytes.as_slice().into();
let from_uint32: RoutingId = 42u32.into();
let restored = RoutingId::from_hex(&previously_printed.to_hex())?;
```

**Options.** Rust `From<T>` trait implementations rather than static factory methods — the
idiomatic conversion mechanism, reached via `.into()` or `RoutingId::from(...)`: `From<&[u8]>`,
`From<&[u8; N]>` (any fixed-size array), `From<&str>` (UTF-8 encode), `From<u32>` (4-byte
big-endian), `From<[u8; 16]>` (16-byte, e.g. UUID bytes) — **every one of these panics** on empty or
over-length input rather than returning a `Result`. `from_hex(&str)` panics the same way;
`try_from_hex(&str) -> Result<Self, ConfigError>` is the non-panicking alternative for hex decoding
specifically. Instance members: `MAX_LEN` (`usize` constant, `255`), `as_bytes()`, `size()`,
`is_empty()`, `to_hex()`, `Display` (formats as printable UTF-8, then 4-byte-as-`u32`, then
16-byte-as-UUID-format, then a `hex:`-prefixed fallback), `PartialEq`/`Eq`/`Hash`/`Copy`/`Clone`
(derived).

**Completion result.** Every `From` conversion and `from_hex` is synchronous and panics on invalid
input. Only `try_from_hex` returns `Result<Self, ConfigError>` instead of panicking.

**When to use.** Use the `From`/`.into()` conversions when the input is already known-valid (for
example, a compile-time string literal); use `try_from_hex` instead of `from_hex` whenever the hex
string comes from outside the program and might be malformed, since `from_hex`/every `From`
conversion panics rather than returning an error.

---

## `version()` / `has(capability)` / `strerror(errnum)`

Reads the native library's build version, checks for an optional build capability, or converts a
native error code to a message.

```rust
let (major, minor, patch) = version();
let has_tls = has("tls");
let message = strerror(errnum);
```

**Options.** `has(capability: &str)` — recognized names include `"tcp"`, `"ipc"`, `"tls"`, `"ws"`,
`"wss"`; any other string returns `false`. `strerror(errnum: i32)`.

**Completion result.** All are synchronous with no error path. `version()` returns `(i32, i32,
i32)`. `has` returns `bool`. `strerror` returns `&'static str`.

**When to use.** Use `version()` to confirm the linked native library matches what the application
expects. Use `has(...)` at startup to branch on optional transports. `strerror` is for diagnostics
alongside a native error code surfaced elsewhere (Errors category).

---

## `Stopwatch::start()` / `AtomicCounter::new()` / `Thread::start(task)`

Creates a high-resolution stopwatch, a thread-safe integer counter, or a running background
thread — three independent utility resources, all declared in `contracts/core/utilities.rs`.

```rust
let mut watch = Stopwatch::start()?;
let partial_us = watch.intermediate();
let total_us = watch.stop();

let counter = AtomicCounter::new()?;
let new_value = counter.increment();

let mut thread = Thread::start(|| do_work())?;
thread.join();
```

**Options.** `Stopwatch::start()`/`AtomicCounter::new()` take no parameters. `Thread::start<F>(task:
F)` where `F: FnOnce() + Send + 'static` runs the task immediately on the new thread. `Stopwatch`:
`intermediate()`/`stop()` (both `u64` microseconds, `&mut self` — `stop` invalidates the handle),
`close()`. `AtomicCounter`: `set(i32)`, `increment()`/`decrement()` (return the counter's *new*
value), `value()`, `close()`. `Thread`: `join(&mut self)` (re-raises the task's panic via
`resume_unwind` if it panicked), `close()`.

**Completion result.** All three constructors return `Result<Self, ConfigError>`. Each type also
implements `Drop`, calling `close()` automatically if not already closed.

**When to use.** Use `AtomicCounter` for a shared count safe across threads. Use `Stopwatch` for
benchmarking — call `intermediate()` any number of times, then `stop()` exactly once. Use `Thread`
for a portable background thread instead of `std::thread` directly, when the zlink runtime needs
to own its lifecycle and re-propagate a task panic through `join()`.

---

## `proxy(...)` / `proxy_steerable(...)` / `sleep(seconds)` / `multipart_close(parts)` / `poll(items, timeout_ms)`

Runs a bidirectional message-forwarding loop between two pollable sources (optionally steerable via
a control source), sleeps the calling thread, closes every message in a multipart slice, or waits
on a fixed array of raw poll items.

```rust
proxy(&frontend, &backend, Some(&capture))?;
proxy_steerable(&frontend, &backend, Some(&capture), &control)?;
sleep(1); // seconds, not milliseconds
multipart_close(&mut parts);
let ready = poll(&mut items, 1000)?;
```

**Options.** `proxy(frontend: &dyn Pollable, backend: &dyn Pollable, capture: Option<&dyn
Pollable>)` and `proxy_steerable(..., control: &dyn Pollable)` take `&dyn Pollable` trait objects
rather than concrete socket types — any built-in socket type implements the sealed `Pollable` trait
(Eventing category). `sleep(seconds: i32)` takes whole seconds directly. `multipart_close(parts:
&mut [Message])`. `poll(items: &mut [PollItem], timeout_ms: i64)` is a standalone one-shot poll
helper distinct from `Poller` (Eventing category) — it fills `revents` on each `PollItem` in place.

**Completion result.** `proxy`/`proxy_steerable` return `Result<(), ConfigError>` — **fallible here**,
unlike other languages where the equivalent call has no error path and simply blocks until
termination. `sleep`/`multipart_close` have no return value. `poll` returns `Result<i32, RecvError>`
(the ready count).

**When to use.** Use `proxy` for a simple fire-and-forget forwarding loop on its own thread. Use
`proxy_steerable` when the application needs to pause/resume/terminate the loop from another thread
via the control source. Use `multipart_close` to release every `Message` in a received or
constructed multipart slice in one call. Use the standalone `poll(...)` for an ad hoc wait across a
small fixed set of raw file descriptors, and `Poller` instead when the watched set changes over
time or sockets/timers need to be multiplexed (Eventing category).

---

See [`contracts/core/`](../../../../bindings/rust/src/contracts/core/),
[`src/lib.rs`](../../../../bindings/rust/src/lib.rs), and the
[Rust binding spec](../../spec/rust/README.en.md) for the full rationale.
