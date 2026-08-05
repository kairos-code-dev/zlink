[한국어](01-core.ko.md) | English

[Reference index](README.en.md)

# 01. Core

This category covers the context lifecycle, context options, routing identity, and the `Zlink`
static facade — the library's process-wide entry points and utility resources. Socket creation
methods on `IContext` are listed here for completeness but detailed under the Sockets category;
poller/timer creation on `Zlink` is listed here but detailed under the Eventing category. The
exact signatures are owned by
[`Contracts/Core/`](../../../../bindings/dotnet/src/Zlink/Contracts/Core/).

---

## `Zlink.CreateContext()`

Creates a messaging context — the factory and owner of sockets, and the prerequisite for every
other entry in this reference.

```csharp
using IContext context = Zlink.CreateContext();
```

**Options.** No parameters.

**Completion result.** Returns `IContext` synchronously. The caller owns the returned context and
must dispose it (`IDisposable`/`IAsyncDisposable`); disposing it terminates anything still open
under it, including sockets created from it.

**When to use.** Call this once per context the application needs; most applications need
exactly one. Every socket must be created from a context and is owned by the caller independent
of the context's own disposal — but disposing the context terminates whatever sockets remain
open under it.

---

## `IContext.Shutdown()` / `IContext.RecalculateAutoHwm()`

Interrupts blocking operations on the context's sockets without disposing them, or forces an
immediate recalculation of automatic high-water marks.

```csharp
context.Shutdown();
context.RecalculateAutoHwm();
```

**Options.** Neither takes parameters.

**Completion result.** Both are synchronous, returning `void`. `Shutdown` interrupts blocking
calls on sockets under this context but does not dispose the context or its sockets — dispose the
context afterward to release resources. `RecalculateAutoHwm` recomputes automatic HWM only for
sockets still configured with an `AutoHwmProfile` (Errors and configuration exceptions aside,
this has no separate failure mode documented).

**When to use.** Call `Shutdown` before disposing a context that has sockets in use across
multiple threads, to avoid a thread blocking on a socket call indefinitely. Call
`RecalculateAutoHwm` after changing the auto-HWM profile or a message-unit option on the context
or a socket, to apply new per-connection sizing immediately instead of waiting for the normal
refresh path.

---

## `IContext.Options`

Reads the context-wide options facade, whose properties govern I/O threads and the defaults
every socket created from the context inherits.

```csharp
context.Options.IoThreads = 8;
context.Options.AutoHwmProfile = AutoHwmProfile.LowLatency;
context.Options.AddThreadAffinityCpu(2);
```

**Options.** `IContextOptions` exposes get/set properties: `IoThreads`, `MaxSockets`,
`SocketLimit` (read-only — the build's hard cap on `MaxSockets`), `ThreadPriority`,
`ThreadSchedulingPolicy`, `MaxMessageSize`, `MessageThreadSize` (read-only), `Blocky`,
`AutoHwmProfile`, `AutoHwmMessageUnitBytes` (`ulong` — see the accounted-byte HWM note in the
Sockets category), `AutoHwmEnabled`, `AutoHwmRecalcDebounce`, `ThreadNamePrefix`, plus the methods
`AddThreadAffinityCpu(cpu)`/`RemoveThreadAffinityCpu(cpu)`.

**Completion result.** All property get/set and the two affinity methods are synchronous.

**When to use.** Adjust these before creating sockets when the defaults don't fit the deployment
(thread count, HWM sizing profile, message-size cap). `AutoHwmProfile`/`AutoHwmEnabled` can be
changed on a live context — pair a change with `RecalculateAutoHwm` (above) to apply it
immediately rather than waiting for the normal refresh path.

---

## `IContext.CreatePairSocket()` / `CreateDealerSocket()` / `CreateRouterSocket()` / `CreatePubSocket()` / `CreateSubSocket()` / `CreateXPubSocket()` / `CreateXSubSocket()` / `CreateStreamSocket()`

Creates a socket of the given type, owned by the caller.

```csharp
using IDealerSocket dealer = context.CreateDealerSocket();
```

**Options.** None of the eight factory methods takes parameters.

**Completion result.** Each returns its corresponding socket interface (`IPairSocket`,
`IDealerSocket`, `IRouterSocket`, `IPubSocket`, `ISubSocket`, `IXPubSocket`, `IXSubSocket`,
`IStreamSocket`) synchronously. The caller owns and must dispose the returned socket
independently of the context.

**When to use.** See the Sockets category for each socket interface's operations, options, and
capability roles — this entry only covers how each is created.

---

## `RoutingId`

A binary-safe value type identifying a messaging peer or route, 1 to 255 bytes.

```csharp
RoutingId fromString = RoutingId.From("worker-3");
RoutingId fromBytes = RoutingId.From(rawBytes);
RoutingId fromUint = RoutingId.From(42u);
RoutingId fromGuid = RoutingId.From(Guid.NewGuid());
RoutingId restored = RoutingId.FromHex(previouslyPrinted.ToHex());
```

**Options.** Static factories: `From(ReadOnlySpan<byte>)`/`From(byte[])` (copy raw bytes as-is),
`From(string)` (UTF-8 encode), `From(uint)` (4-byte big-endian), `From(Guid)` (16-byte
big-endian), `FromHex(string)` (restores bytes `ToHex()` printed). Instance members:
`Size`/`IsEmpty`, `ToBytes()` (internal-storage-backed view), `ToHex()`, `TryToUInt32(out uint)`,
`TryToGuid(out Guid)`, `ToString()` (display form — printable UTF-8, then `uint`, then `Guid`,
then a `hex:`-prefixed fallback), value equality (`Equals`/`==`/`!=`/`GetHashCode`).

**Completion result.** Every factory and accessor is synchronous. Out-of-range byte length (not
1..255) throws `ArgumentOutOfRangeException`; a malformed hex string to `FromHex` throws
`ArgumentException`.

**When to use.** Use `From(string)` for a human-assigned identity, `From(uint)`/`From(Guid)` for
a numeric or GUID-shaped identity, and raw `From(byte[])`/`From(ReadOnlySpan<byte>)` when the
identity is already binary. Use `ToHex()`/`FromHex()` specifically for a durable raw-byte round
trip — `ToString()` is for display only and is not guaranteed reversible (it prefers a printable
UTF-8 interpretation before falling back to numeric/GUID/hex forms).

---

## `Zlink.Version()` / `Zlink.Strerror(int)` / `Zlink.Has(string)`

Reads the native library's build version, converts a native error code to a message, or checks
for an optional build capability.

```csharp
var (major, minor, patch) = Zlink.Version();
string message = Zlink.Strerror(errnum);
bool hasTls = Zlink.Has("tls");
```

**Options.** `Strerror` takes `errnum` (an `int` error code). `Has` takes `capability` (a
string — `"tcp"`, `"ipc"`, `"tls"`, `"ws"`, `"wss"` are the recognized names; any other string
returns `false`).

**Completion result.** All three are synchronous. `Version()` returns a
`(int Major, int Minor, int Patch)` tuple. `Strerror` returns a `string`. `Has` returns `bool`.

**When to use.** Use `Version()` to confirm the linked native library matches what the
application expects, especially when it's loaded dynamically. Use `Has(...)` at startup to branch
on optional transports rather than assuming every one is compiled in. `Strerror` is for
diagnostics alongside a native error code surfaced elsewhere.

---

## `Zlink.CreateAtomicCounter()` / `Zlink.CreateStopwatch()` / `Zlink.CreateThread(Action)`

Creates a thread-safe integer counter, a high-resolution stopwatch, or a running background
thread — three independent utility resources.

```csharp
using IAtomicCounter counter = Zlink.CreateAtomicCounter();
int newValue = counter.Increment();

using IZlinkStopwatch watch = Zlink.CreateStopwatch();
ulong partialUs = watch.Intermediate();
ulong totalUs = watch.Stop();

using IZlinkThread thread = Zlink.CreateThread(() => DoWork());
thread.Join();
```

**Options.** `CreateAtomicCounter()`/`CreateStopwatch()` take no parameters. `CreateThread(Action
task)` takes the task to run immediately on the new thread. `IAtomicCounter` provides
`Value` (get), `Set(value)`, `Increment()`, `Decrement()` — the last two return the counter's
*new* value after the operation, not the prior value. `IZlinkStopwatch` provides
`Intermediate()` and `Stop()`, both in microseconds. `IZlinkThread` provides `Join()` (blocks
until the task finishes; repeated calls are no-ops) and `Close()` (releases the handle, joining
first if still running).

**Completion result.** All three factories return their resource interface synchronously; the
caller owns and must dispose each one (`IDisposable`/`IAsyncDisposable`).

**When to use.** Use `CreateAtomicCounter` for a shared count safe across threads.
`CreateStopwatch` for benchmarking/profiling — call `Intermediate()` any number of times for
successive readings, then `Stop()` exactly once to finish and release it. `CreateThread` for a
portable background thread instead of a platform-specific API — dispose it (or `Join()` then
`Close()`) to release it.

---

## `Zlink.Proxy(...)` / `Zlink.ProxySteerable(...)` / `Zlink.Sleep(TimeSpan)` / `Zlink.MultipartClose(...)`

Runs a bidirectional message-forwarding loop between two sockets (optionally steerable via a
control socket), sleeps the calling thread, or disposes every message in a multipart payload.

```csharp
Zlink.Proxy(frontend, backend, capture); // capture may be null; blocks until context termination
Zlink.ProxySteerable(frontend, backend, capture, control); // control accepts runtime commands
Zlink.Sleep(TimeSpan.FromSeconds(1));
Zlink.MultipartClose(parts);
```

**Options.** `Proxy`/`ProxySteerable` take `frontend`/`backend` (required `IZlinkSocket`),
`capture` (optional — receives a copy of every forwarded message); `ProxySteerable` additionally
takes a required `control` socket. `Sleep` takes a `TimeSpan` duration. `MultipartClose` takes
`IReadOnlyList<Message>`.

**Completion result.** All four are synchronous with no return value. `Proxy`/`ProxySteerable`
block the calling thread until the context is terminated (or, for `ProxySteerable`, until a
`TERMINATE` control command or an error ends the loop) — run either on a dedicated thread.

**When to use.** Use `Proxy` for a simple fire-and-forget forwarding loop on its own thread.
Use `ProxySteerable` when the application needs to pause/resume/terminate the loop, or pull
statistics, from another thread via the control socket. Use `MultipartClose` to release every
`Message` in a received or constructed multipart array in one call instead of a hand-written
loop.

---

## `Zlink.CreatePoller()` / `Zlink.CreateTimer()`

Creates a reusable poller, or a standalone timer.

```csharp
using IPoller poller = Zlink.CreatePoller();
using IZlinkTimer timer = Zlink.CreateTimer();
```

**Options.** Neither factory takes parameters (a `CreateTimer(ISpot)` overload also exists for
binding a timer to a Spot's lifecycle — Service category).

**Completion result.** Both return their resource interface (`IPoller`, `IZlinkTimer`)
synchronously; the caller owns and must dispose each.

**When to use.** See the Eventing category for `IPoller`'s and `IZlinkTimer`'s own operations —
this entry only covers creation.

---

## `Zlink.UnhandledCallbackException`

A static event raised when a user callback throws.

```csharp
Zlink.UnhandledCallbackException += ex => logger.LogError(ex, "callback failed");
```

**Options.** Subscribes/unsubscribes an `Action<Exception>`.

**Completion result.** Synchronous add/remove. The event fires on the background dispatch thread
running the callback that threw — that thread cannot propagate the exception back to the
original caller, since the callback runs asynchronously relative to whatever registered it.

**When to use.** Subscribe here to observe exceptions from any registered callback (stream
packet handler, monitor handler, poll handler, SPOT dispatch handler, request/reply callback,
etc. — Sockets/Eventing/Service categories) that would otherwise be silently lost, since none of
those callbacks run on a thread that can surface the exception to its caller directly.

---

See [`Contracts/Core/`](../../../../bindings/dotnet/src/Zlink/Contracts/Core/) and the
[.NET binding spec](../../spec/dotnet/README.en.md) for the full rationale.
