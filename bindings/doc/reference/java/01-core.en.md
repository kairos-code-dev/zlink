[한국어](01-core.ko.md) | English

[Reference index](README.en.md)

# 01. Core

This category covers the context lifecycle, context options, routing identity, and the `Zlink`
static facade. Socket creation methods on `Context` are listed here for completeness but detailed
under the Sockets category; poller/timer creation on `Zlink` is listed here but detailed under the
Eventing category. The exact signatures are owned by
[`contracts/core/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/core/).

---

## `Zlink.createContext()`

Creates a messaging context — the factory and owner of sockets.

```java
try (Context context = Zlink.createContext()) {
    // ...
}
```

**Options.** No parameters.

**Completion result.** Returns `Context` synchronously. `Context extends AutoCloseable`; the caller
owns it and must close it — closing terminates anything still open under it, including sockets
created from it.

**When to use.** Call once per context the application needs; most applications need exactly one.

---

## `Context.shutdown()` / `Context.recalculateAutoHwm()`

Interrupts blocking operations on the context's sockets without closing them, or forces an
immediate recalculation of automatic high-water marks.

```java
context.shutdown();
context.recalculateAutoHwm();
```

**Options.** Neither takes parameters.

**Completion result.** Both are synchronous, returning `void`. `shutdown()` interrupts blocking
calls on sockets under this context but does not close the context or its sockets. `recalculateAutoHwm()`
recomputes automatic HWM only for sockets still configured with an `AutoHwmProfile`.

**When to use.** Call `shutdown()` before closing a context with sockets in use across multiple
threads, to avoid a thread blocking on a socket call indefinitely. Call `recalculateAutoHwm()`
after changing the auto-HWM profile or a message-unit option, to apply new sizing immediately.

---

## `Context.options()` / `ContextOptions`

Reads the context-wide options facade, whose properties govern I/O threads and the defaults every
socket created from the context inherits.

```java
ContextOptions options = context.options();
options.ioThreads(8);
options.autoHwmProfile(AutoHwmProfile.LOW_LATENCY);
options.addThreadAffinityCpu(2);
```

**Options.** `ContextOptions` is a concrete class with a public constructor
(`new ContextOptions(context)`), though `context.options()` is the normal path. Paired getter/setter
methods: `ioThreads()`/`ioThreads(int)`, `maxSockets()`/`maxSockets(int)`, `threadPriority()`/
`threadPriority(int)`, `threadSchedulingPolicy()`/`threadSchedulingPolicy(int)`,
`threadNamePrefix()`/`threadNamePrefix(String)`, `maxMessageSize()`/`maxMessageSize(int)`,
`blocky()`/`blocky(boolean)`, `autoHwmEnabled()`/`autoHwmEnabled(boolean)`,
`autoHwmRecalcDebounce()`/`autoHwmRecalcDebounce(Duration)`, `autoHwmProfile()`/
`autoHwmProfile(AutoHwmProfile)`, `autoHwmMessageUnitBytes()`/`autoHwmMessageUnitBytes(long)` (the
unsigned 64-bit bit pattern; zero selects the socket-type default). Read-only: `socketLimit()`,
`messageThreadSize()`. Setter-only: `addThreadAffinityCpu(int)`/`removeThreadAffinityCpu(int)`.

**Completion result.** All property get/set are synchronous.

**When to use.** Adjust before creating sockets when the defaults don't fit the deployment. Pair an
`autoHwmProfile`/`autoHwmEnabled` change with `Context.recalculateAutoHwm()` to apply it
immediately.

---

## `Context.createPairSocket()` / `createDealerSocket()` / `createRouterSocket()` / `createPubSocket()` / `createSubSocket()` / `createXPubSocket()` / `createXSubSocket()` / `createStreamSocket()`

Creates a socket of the given type, owned by the caller.

```java
try (DealerSocket dealer = context.createDealerSocket()) {
    // ...
}
```

**Options.** None of the eight factory methods takes parameters.

**Completion result.** Each returns its corresponding socket interface synchronously. The caller
owns and must close the returned socket independently of the context.

**When to use.** See the Sockets category for each socket interface's operations, options, and
capability roles — this entry only covers how each is created.

---

## `RoutingId`

A binary-safe value type identifying a messaging peer or route, 1 to 255 bytes.

```java
RoutingId fromString = RoutingId.from("worker-3");
RoutingId fromBytes = RoutingId.from(rawBytes);
RoutingId fromRange = RoutingId.from(buffer, offset, length);
RoutingId fromUint32 = RoutingId.from(42L);
RoutingId fromUuid = RoutingId.from(UUID.randomUUID());
RoutingId restored = RoutingId.fromHex(previouslyPrinted.toHex());
```

**Options.** Static factories: `from(byte[])` (copies the full array), `from(byte[] value, int
offset, int length)` (copies the selected byte range — a Java-specific overload not present in
dotnet/cpp), `from(String)` (UTF-8 encode), `from(long)` (4-byte big-endian from an unsigned
32-bit value — throws `IllegalArgumentException` if the value doesn't fit in 32 bits), `from(UUID)`
(16-byte), `fromHex(String)` (restores bytes `toHex()` printed). Instance members: `size()`,
`toBytes()` (defensive copy), `toHex()`, `toString()` (printable UTF-8, then 4-byte-as-unsigned-int,
then 16-byte-as-UUID, then a `hex:`-prefixed fallback), `equals`/`hashCode`. `MAX_LENGTH` (`255`) is
a public constant. Internally, `RoutingId` maintains a per-thread trusted-bytes cache to avoid
reallocating on the receive hot path — this is not part of the public contract surface.

**Completion result.** Every factory and accessor is synchronous. Out-of-range length throws
`IllegalArgumentException`; a malformed hex string to `fromHex` throws the same.

**When to use.** Use `from(String)` for a human-assigned identity, `from(long)`/`from(UUID)` for a
numeric or UUID-shaped identity, and the raw byte overloads (including the range overload) when the
identity is already binary or is a slice of a larger buffer. Use `toHex()`/`fromHex()` specifically
for a durable raw-byte round trip — `toString()` is for display only and is not guaranteed
reversible.

---

## `Zlink.strerror(int)` / `Zlink.has(String)` / `Zlink.version()` / `ZlinkVersion.get()`

Converts a native error code to a message, checks for an optional build capability, or reads the
native library's build version.

```java
String message = Zlink.strerror(errnum);
boolean hasTls = Zlink.has("tls");
int[] version = Zlink.version();
```

**Options.** `strerror(int errnum)`. `has(String capability)` — recognized names include `"tcp"`,
`"ipc"`, `"tls"`, `"ws"`, `"wss"`; any other string returns `false`. `version()`/`ZlinkVersion.get()`
are equivalent — `ZlinkVersion` is a thin convenience wrapper delegating to `Zlink.version()`.
`Zlink.errno()` exists in source but has no `public` modifier — it is not reachable from application
code.

**Completion result.** All are synchronous. `strerror` returns a `String`. `has` returns `boolean`.
`version()`/`ZlinkVersion.get()` return `int[]` (major/minor/patch).

**When to use.** Use `version()` to confirm the linked native library matches what the application
expects. Use `has(...)` at startup to branch on optional transports. `strerror` is for diagnostics
alongside a native error code surfaced elsewhere (Errors category).

---

## `Zlink.createAtomicCounter()` / `Zlink.createStopwatch()` / `Zlink.createThread(Runnable)`

Creates a thread-safe integer counter, a high-resolution stopwatch, or a running background
thread — three independent utility resources.

```java
try (AtomicCounter counter = Zlink.createAtomicCounter()) {
    int newValue = counter.increment();
}

try (ZlinkStopwatch watch = Zlink.createStopwatch()) {
    Duration partial = watch.intermediate();
    Duration total = watch.stop();
}

try (ZlinkThread thread = Zlink.createThread(() -> doWork())) {
    thread.join();
}
```

**Options.** `createAtomicCounter()`/`createStopwatch()` take no parameters. `createThread(Runnable
task)` takes the task to run immediately on the new thread. `AtomicCounter` provides `value()`
(get), `set(int)`, `increment()`/`decrement()` (return the counter's *new* value). `ZlinkStopwatch`
provides `intermediate()`/`stop()`, both returning `Duration`. `ZlinkThread` provides `join()`
(blocks until the task finishes).

**Completion result.** All three factories return their resource interface synchronously; each is
`AutoCloseable` and the caller must close it.

**When to use.** Use `AtomicCounter` for a shared count safe across threads. `ZlinkStopwatch` for
benchmarking — call `intermediate()` any number of times, then `stop()` exactly once.
`createThread` for a portable background thread instead of `java.lang.Thread` directly, when the
zlink runtime needs to own its lifecycle.

---

## `Zlink.proxy(...)` / `Zlink.proxySteerable(...)` / `Zlink.sleep(Duration)`

Runs a bidirectional message-forwarding loop between two sockets (optionally steerable via a
control socket), or sleeps the calling thread.

```java
Zlink.proxy(frontend, backend, capture); // capture may be null; blocks until context termination
Zlink.proxySteerable(frontend, backend, capture, control);
Zlink.sleep(Duration.ofSeconds(1));
```

**Options.** `proxy(Socket frontend, Socket backend, Socket capture)` — `capture` may be `null`.
`proxySteerable(Socket frontend, Socket backend, Socket capture, Socket control)`. Only
`sleep(Duration)` is public — `Zlink.sleep(int seconds)` and `Zlink.multipartClose(Message[])` exist
in source but have no `public` modifier and are not reachable from application code, unlike
dotnet's public `Zlink.Sleep(TimeSpan)`/`Zlink.MultipartClose(...)` pairing.

**Completion result.** All are synchronous with no return value. `proxy`/`proxySteerable` block the
calling thread until the context is terminated (or, for `proxySteerable`, until a control command or
error ends the loop) — run either on a dedicated thread.

**When to use.** Use `proxy` for a simple fire-and-forget forwarding loop. Use `proxySteerable` when
the application needs to pause/resume/terminate the loop from another thread via the control
socket.

---

See [`contracts/core/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/core/) and
the [Java binding spec](../../spec/java/README.en.md) for the full rationale.
