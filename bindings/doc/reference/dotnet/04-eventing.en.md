[한국어](04-eventing.ko.md) | English

[Reference index](README.en.md)

# 04. Eventing

This category covers socket monitoring, the reusable poller, and standalone timers — created via
`ISocket.MonitorOpen(...)` (Sockets category) and `Zlink.CreatePoller()`/`Zlink.CreateTimer()`
(Core category) respectively. The exact signatures are owned by
[`Contracts/Eventing/`](../../../../bindings/dotnet/src/Zlink/Contracts/Eventing/).

---

## `ISocketMonitor`

Observes a socket's connection lifecycle events and reads its current status.

```csharp
using ISocketMonitor monitor = socket.MonitorOpen(SocketEvent.Connected | SocketEvent.Disconnected);
monitor.OnEvent(e => logger.LogInformation("{Event} {Remote}", e.Event, e.RemoteAddr));
MonitorStatus status = monitor.Status();
```

**Options.** `OnEvent(Action<MonitorEvent> handler)` (background-dispatch-thread callback),
`Recv(RecvFlags flags = RecvFlags.None)` (returns `MonitorEvent?`, null when
`RecvFlags.DontWait` and nothing pending), `Status()` (no parameters), `Close()`.
`MonitorEvent(MonitorEventType Event, uint Value, RoutingId? RoutingId, string LocalAddr, string
RemoteAddr)` is the record delivered by both `OnEvent` and `Recv` — `Value` is event-specific (an
error code or a reconnect interval, for example), `RoutingId` is present only when the event
carries one.

**Completion result.** All members are synchronous. `ISocketMonitor` is `IDisposable`/
`IAsyncDisposable`; `Close()` releases resources without waiting on disposal semantics.

**When to use.** Use `OnEvent` for a passive lifecycle observer registered once; use `Recv` for a
pull-based drain loop instead. Use `Status()` for a point-in-time snapshot rather than reconstructing
current state from a stream of events.

---

## `MonitorStatus`

A snapshot of a socket's monitored state and auto-high-water-mark telemetry, returned by
`ISocketMonitor.Status()`.

**Options.** No parameters — every member below is a read-only property.

| Group | Members |
|---|---|
| ABI identity | `AbiVersion`, `StructSize` (uint) — mirrors the native `zlink_monitor_status_t` ABI version 2 |
| Source/state | `SourceKind` (`MonitorSourceKind`), `StateFlags` (`MonitorStateFlags`), `DetailFlags` (`MonitorStatusDetailFlags`), `IsReady` (computed: `SourceKind == Socket && StateFlags.Ready`) |
| Pending counts | `SndPendingMsgs`, `RcvPendingMsgs` (`ulong` — count diagnostics, never share a name with a byte field) |
| Auto-HWM config | `AutoHwmEnabled` (bool), `AutoHwmProfile` (`AutoHwmProfile`), `AutoHwmRole`, `AutoHwmPolicyClass`, `AutoHwmUnitBudgetBytes`, `AutoHwmSizeCap`, `AutoHwmSocketMessageSlots` |
| Connection bucket | `AutoHwmConnectionBucketEnabled`, `AutoHwmConnectionBucketCount`, `AutoHwmConnectionBucketIndex`, `AutoHwmConnectionBucketHwm4K`, `AutoHwmConnectionBucketHysteresisRetained` |
| Auto-HWM plan (bytes) | `AutoHwmEffectiveMessageBytes`, `AutoHwmPlannedSendHighWaterMarkBytes`, `AutoHwmPlannedReceiveHighWaterMarkBytes`, `AutoHwmAppliedSendHighWaterMarkBytes`, `AutoHwmAppliedReceiveHighWaterMarkBytes`, `AutoHwmEffectiveSndbuf`, `AutoHwmEffectiveRcvbuf` |
| Auto-HWM recalc | `AutoHwmLastRecalcMs`, `AutoHwmLastRecalcReason` (`AutoHwmRecalcReason`), `AutoHwmSendBlockedRatioPpm` |
| Auto-HWM deferred shrink | `AutoHwmDeferredSendHighWaterMarkBytes`/`AutoHwmDeferredReceiveHighWaterMarkBytes` (valid only when the matching `AutoHwmDeferredSendHighWaterMarkValid`/`AutoHwmDeferredReceiveHighWaterMarkValid` is true) |
| In-flight/charging | `SendBytesInFlight`, `ReceiveBytesInFlight`, `MinimumCoreMessageChargeBytes`, `OversizeMessageAdmissionCount`, `OversizeMessageAdmissionMaxBytes` |

**Completion result.** All properties are synchronous reads of an immutable snapshot. Every
byte-valued field is `ulong`; `AutoHwmProfile` mirrors the enum documented in the Sockets category.

**When to use.** Read `IsReady` for a quick readiness check instead of decoding `StateFlags`
directly. Use the connection-bucket and auto-HWM-plan groups when diagnosing why a socket's
effective send/receive HWM differs from its configured `CommonSocketOptions` value (Sockets
category) — the deferred-shrink fields explain a HWM that hasn't dropped immediately after a
policy change.

---

## `IPoller`

Multiplexes sockets, file descriptors, and timers on a single reusable wait.

```csharp
using IPoller poller = Zlink.CreatePoller();
poller.Add(dealer, PollEventFlags.PollIn, slot: 1);
poller.Add(timer, slot: 2);
Span<PollEvent> ready = stackalloc PollEvent[8];
int count = poller.Wait(ready, TimeSpan.FromSeconds(1));
```

**Options.** `Size` (read-only `int`). `Add(IZlinkSocket socket, PollEventFlags events, nuint
slot)`, `AddFd(int fd, PollEventFlags events, nuint slot)`, `Add(IZlinkTimer timer, nuint slot)` —
`slot` is a caller token echoed back in the matching `PollEvent`. `Modify(IZlinkSocket socket,
PollEventFlags events)`, `ModifyFd(int fd, PollEventFlags events)`. `Remove(IZlinkSocket socket)`/
`Remove(IZlinkTimer timer)`/`Remove(int fd)` (each returns `bool`, true when it was registered).
`Clear()`. `Close()`. `Wait(Span<PollEvent> destination, TimeSpan timeout)`.

**Completion result.** All registration/removal members are synchronous with no blocking. `Wait`
blocks up to `timeout`, writing up to `destination.Length` results and returning the count written
(`0` on timeout). `IPoller` is `IDisposable`/`IAsyncDisposable`.

**When to use.** Use one poller across a service's lifetime rather than creating one per wait —
`Add`/`Remove`/`Modify` let the watched set change between calls to `Wait`. Prefer `Modify`
over `Remove` + `Add` when only the watched events change, to avoid losing the source's position.

---

## `PollEvent`

One ready source reported by an `IPoller.Wait` call.

**Options.** No parameters — a read-only value type. Members: `SourceKind` (`PollSourceKind`:
`Socket`/`Fd`/`Timer`), `Slot` (`nuint`, the caller token supplied at registration), `Revents`
(`PollEventFlags`, the events that actually fired), `Fd` (`int`, populated for `Fd`-kind sources).

**Completion result.** Synchronous — a plain readonly struct, no disposal.

**When to use.** Branch on `SourceKind`/`Slot` to route each `Wait` result back to the socket,
descriptor, or timer it corresponds to, since `Wait` reports heterogeneous source kinds in one
array.

---

## `IZlinkTimer`

A standalone timer that fires on an interval and can be awaited (via `Recv`) or driven through a
poller.

```csharp
using IZlinkTimer timer = Zlink.CreateTimer();
timer.OnFire((t, count) => logger.LogInformation("fired {Count} times", count));
timer.Start(TimeSpan.FromSeconds(1), repeatCount: 0);
```

**Options.** `Start(TimeSpan interval, ulong repeatCount)` (`repeatCount` sets how many times it
fires — see the source for the sentinel meaning "repeat indefinitely"), `Stop()` (restartable via
`Start`), `Recv(RecvFlags flags = RecvFlags.None)` (returns `ulong?` — the cumulative fire count,
null when `RecvFlags.DontWait` and nothing pending), `OnFire(Action<IZlinkTimer, ulong> handler)`
(background-dispatch-thread callback receiving the timer and the fire count), `Close()`.

**Completion result.** All members are synchronous. `IZlinkTimer` is `IDisposable`/
`IAsyncDisposable`.

**When to use.** Use `OnFire` for a passive interval callback; use `Recv` to poll/await
expirations from a receive loop instead, or register the timer with `IPoller.Add(IZlinkTimer,
nuint)` to multiplex it alongside sockets on one wait.

---

## `ZlinkPoll.Poll(...)`

Static one-shot helpers that wait for readiness across several sockets or monitors at once,
without creating a reusable `IPoller`.

```csharp
int ready = ZlinkPoll.Poll(new IZlinkSocket[] { dealer, sub }, timeoutMs: 1000);
```

**Options.** Four overloads: `Poll(IReadOnlyList<IZlinkSocket> sockets, int timeoutMs)` (readable
check only); `Poll(IReadOnlyList<IZlinkSocket> sockets, IReadOnlyList<PollEventFlags> events,
Span<PollEventFlags> revents, int timeoutMs)` (per-socket requested events, fired events written
into `revents` at matching indexes); and the same two overloads for `IReadOnlyList<ISocketMonitor>`
in place of sockets. A negative `timeoutMs` blocks indefinitely.

**Completion result.** Synchronous; each overload returns the count of ready sources (`0` on
timeout).

**When to use.** Use `ZlinkPoll.Poll` for an ad hoc, one-off wait across a small fixed set of
sockets/monitors; use `IPoller` instead when the watched set changes over time or timers need to
be multiplexed alongside sockets.

---

## Eventing enums

Shared enums referenced across every entry above.

| Enum | Used by | Values |
|---|---|---|
| `SocketEvent` (`[Flags]`) | `ISocket.MonitorOpen(SocketEvent)` (Sockets category) | `Connected`, `ConnectDelayed`, `ConnectRetried`, `Listening`, `BindFailed`, `Accepted`, `AcceptFailed`, `Closed`, `CloseFailed`, `Disconnected`, `MonitorStopped`, `HandshakeFailedNoDetail`, `ConnectionReady`, `HandshakeFailedProtocol`, `HandshakeFailedAuth`, `PeerWeightChanged`, `All` |
| `MonitorEventType` | `MonitorEvent.Event` | Mirrors `SocketEvent`'s lifecycle values (no `All`) |
| `MonitorSourceKind` | `MonitorStatus.SourceKind` | `Socket` |
| `MonitorStateFlags` (`[Flags]`, `uint`) | `MonitorStatus.StateFlags` | `None`, `Ready`, `BoundReady`, `Closed` |
| `MonitorStatusDetailFlags` (`[Flags]`, `uint`) | `MonitorStatus.DetailFlags` | `None`, `SendPendingMessages`, `ReceivePendingMessages`, `AutoHwmBudget`, `AutoHwmBuffers` |
| `AutoHwmRecalcReason` (`uint`) | `MonitorStatus.AutoHwmLastRecalcReason` | `None`, `Initial`, `RoleChange`, `PolicyToggle`, `Refresh`, `DeferredShrink` |
| `PollSourceKind` | `PollEvent.SourceKind` | `Socket`, `Fd`, `Timer` |
| `PollEventFlags` | `IPoller.Add`/`Modify`/`Wait`, `ZlinkPoll.Poll` | `None`, `PollIn`, `PollOut`, `PollErr`, `PollPri`, `PollCompletion` |

---

See [`Contracts/Eventing/`](../../../../bindings/dotnet/src/Zlink/Contracts/Eventing/) and the
[.NET binding spec](../../spec/dotnet/README.en.md) for the full rationale.
