한국어 | [English](04-eventing.en.md)

[레퍼런스 목차](README.ko.md)

# 04. Eventing

이 category는 socket monitoring, 재사용 가능한 poller, standalone timer를 다룬다 —
각각 `ISocket.MonitorOpen(...)`(Sockets category)와
`Zlink.CreatePoller()`/`Zlink.CreateTimer()`(Core category)로 생성된다. 정확한
signature는 [`Contracts/Eventing/`](../../../../bindings/dotnet/src/Zlink/Contracts/Eventing/)가
소유한다.

---

## `ISocketMonitor`

socket의 connection lifecycle event를 관찰하고 현재 상태를 읽는다.

```csharp
using ISocketMonitor monitor = socket.MonitorOpen(SocketEvent.Connected | SocketEvent.Disconnected);
monitor.OnEvent(e => logger.LogInformation("{Event} {Remote}", e.Event, e.RemoteAddr));
MonitorStatus status = monitor.Status();
```

**Options.** `OnEvent(Action<MonitorEvent> handler)`(background-dispatch-thread 콜백),
`Recv(RecvFlags flags = RecvFlags.None)`(`MonitorEvent?` 반환, `RecvFlags.DontWait`고
대기 중인 게 없으면 null), `Status()`(인자 없음), `Close()`.
`MonitorEvent(MonitorEventType Event, uint Value, RoutingId? RoutingId, string
LocalAddr, string RemoteAddr)`는 `OnEvent`와 `Recv` 둘 다가 전달하는 record다 —
`Value`는 event별로 다르고(예: 에러 코드나 reconnect interval), `RoutingId`는 event가
가진 경우에만 존재한다.

**Completion result.** 모든 member는 동기다. `ISocketMonitor`는
`IDisposable`/`IAsyncDisposable`이다 — `Close()`는 disposal semantic을 기다리지 않고
resource를 반환한다.

**선택 기준.** 한 번 등록하는 수동적 lifecycle observer엔 `OnEvent`를, pull 기반
drain loop엔 대신 `Recv`를 쓴다. event 스트림에서 현재 상태를 재구성하는 대신
시점 스냅샷엔 `Status()`를 쓴다.

---

## `MonitorStatus`

`ISocketMonitor.Status()`가 반환하는, socket의 monitored 상태와
auto-high-water-mark telemetry 스냅샷.

**Options.** 인자 없음 — 아래 모든 member는 읽기 전용 속성이다.

| 그룹 | Member |
|---|---|
| ABI identity | `AbiVersion`, `StructSize`(uint) — native `zlink_monitor_status_t` ABI version 2를 반영 |
| Source/state | `SourceKind`(`MonitorSourceKind`), `StateFlags`(`MonitorStateFlags`), `DetailFlags`(`MonitorStatusDetailFlags`), `IsReady`(계산값: `SourceKind == Socket && StateFlags.Ready`) |
| Pending count | `SndPendingMsgs`, `RcvPendingMsgs`(`ulong` — count 진단값, byte 필드와 이름을 공유하지 않음) |
| Auto-HWM 설정 | `AutoHwmEnabled`(bool), `AutoHwmProfile`(`AutoHwmProfile`), `AutoHwmRole`, `AutoHwmPolicyClass`, `AutoHwmUnitBudgetBytes`, `AutoHwmSizeCap`, `AutoHwmSocketMessageSlots` |
| Connection bucket | `AutoHwmConnectionBucketEnabled`, `AutoHwmConnectionBucketCount`, `AutoHwmConnectionBucketIndex`, `AutoHwmConnectionBucketHwm4K`, `AutoHwmConnectionBucketHysteresisRetained` |
| Auto-HWM plan(byte) | `AutoHwmEffectiveMessageBytes`, `AutoHwmPlannedSendHighWaterMarkBytes`, `AutoHwmPlannedReceiveHighWaterMarkBytes`, `AutoHwmAppliedSendHighWaterMarkBytes`, `AutoHwmAppliedReceiveHighWaterMarkBytes`, `AutoHwmEffectiveSndbuf`, `AutoHwmEffectiveRcvbuf` |
| Auto-HWM recalc | `AutoHwmLastRecalcMs`, `AutoHwmLastRecalcReason`(`AutoHwmRecalcReason`), `AutoHwmSendBlockedRatioPpm` |
| Auto-HWM deferred shrink | `AutoHwmDeferredSendHighWaterMarkBytes`/`AutoHwmDeferredReceiveHighWaterMarkBytes`(대응하는 `AutoHwmDeferredSendHighWaterMarkValid`/`AutoHwmDeferredReceiveHighWaterMarkValid`가 true일 때만 유효) |
| In-flight/과금 | `SendBytesInFlight`, `ReceiveBytesInFlight`, `MinimumCoreMessageChargeBytes`, `OversizeMessageAdmissionCount`, `OversizeMessageAdmissionMaxBytes` |

**Completion result.** 모든 property는 불변 스냅샷에 대한 동기 읽기다. byte 값을 갖는
모든 필드는 `ulong`이다. `AutoHwmProfile`은 Sockets category에 문서화된 enum을
그대로 반영한다.

**선택 기준.** `StateFlags`를 직접 디코딩하는 대신 빠른 준비 상태 확인엔 `IsReady`를
읽는다. socket의 실제 send/receive HWM이 설정한 `CommonSocketOptions` 값(Sockets
category)과 다른 이유를 진단할 땐 connection-bucket과 auto-HWM-plan 그룹을 쓴다 —
deferred-shrink 필드는 정책 변경 직후 HWM이 즉시 내려가지 않는 이유를 설명한다.

---

## `IPoller`

socket, file descriptor, timer를 하나의 재사용 가능한 wait로 multiplex한다.

```csharp
using IPoller poller = Zlink.CreatePoller();
poller.Add(dealer, PollEventFlags.PollIn, slot: 1);
poller.Add(timer, slot: 2);
Span<PollEvent> ready = stackalloc PollEvent[8];
int count = poller.Wait(ready, TimeSpan.FromSeconds(1));
```

**Options.** `Size`(읽기 전용 `int`). `Add(IZlinkSocket socket, PollEventFlags events,
nuint slot)`, `AddFd(int fd, PollEventFlags events, nuint slot)`, `Add(IZlinkTimer
timer, nuint slot)` — `slot`은 대응하는 `PollEvent`로 그대로 되돌아오는 caller
token이다. `Modify(IZlinkSocket socket, PollEventFlags events)`, `ModifyFd(int fd,
PollEventFlags events)`. `Remove(IZlinkSocket socket)`/`Remove(IZlinkTimer timer)`/
`Remove(int fd)`(각각 `bool` 반환, 등록돼 있었으면 true). `Clear()`. `Close()`.
`Wait(Span<PollEvent> destination, TimeSpan timeout)`.

**Completion result.** 모든 등록/제거 member는 block 없이 동기다. `Wait`는 `timeout`
까지 block하며, `destination.Length`까지 결과를 쓰고 쓴 개수를 반환한다(timeout이면
`0`). `IPoller`는 `IDisposable`/`IAsyncDisposable`이다.

**선택 기준.** wait마다 새로 만드는 대신 서비스 수명 전체에서 poller 하나를 쓴다 —
`Add`/`Remove`/`Modify`로 `Wait` 호출 사이에 감시 대상 집합을 바꿀 수 있다. 감시하는
event만 바뀔 땐 source의 위치를 잃지 않도록 `Remove` + `Add` 대신 `Modify`를
선호한다.

---

## `PollEvent`

`IPoller.Wait` 호출이 보고하는 준비된 source 하나.

**Options.** 인자 없음 — 읽기 전용 value type. Member: `SourceKind`(`PollSourceKind`:
`Socket`/`Fd`/`Timer`), `Slot`(`nuint`, 등록 시 제공한 caller token), `Revents`
(`PollEventFlags`, 실제로 발생한 event), `Fd`(`int`, `Fd` kind source에서만 채워짐).

**Completion result.** 동기다 — 순수한 readonly struct, dispose 없음.

**선택 기준.** `Wait`가 하나의 배열에서 여러 종류의 source를 보고하므로,
`SourceKind`/`Slot`으로 분기해 각 `Wait` 결과를 대응하는 socket·descriptor·timer로
연결한다.

---

## `IZlinkTimer`

interval마다 fire하며 (`Recv`로) await하거나 poller를 통해 구동할 수 있는 standalone
timer.

```csharp
using IZlinkTimer timer = Zlink.CreateTimer();
timer.OnFire((t, count) => logger.LogInformation("fired {Count} times", count));
timer.Start(TimeSpan.FromSeconds(1), repeatCount: 0);
```

**Options.** `Start(TimeSpan interval, ulong repeatCount)`(`repeatCount`가 몇 번
fire할지 정한다 — "무기한 반복"을 뜻하는 sentinel 값은 소스 참고), `Stop()`(`Start`로
재시작 가능), `Recv(RecvFlags flags = RecvFlags.None)`(`ulong?` 반환 — 누적 fire
count, `RecvFlags.DontWait`고 대기 중인 게 없으면 null), `OnFire(Action<IZlinkTimer,
ulong> handler)`(timer와 fire count를 받는 background-dispatch-thread 콜백),
`Close()`.

**Completion result.** 모든 member는 동기다. `IZlinkTimer`는
`IDisposable`/`IAsyncDisposable`이다.

**선택 기준.** 수동적 interval 콜백엔 `OnFire`를, receive loop에서 만료를
poll·await하려면 대신 `Recv`를, 하나의 wait에서 socket과 함께 timer를 multiplex하려면
`IPoller.Add(IZlinkTimer, nuint)`로 등록한다.

---

## `ZlinkPoll.Poll(...)`

재사용 가능한 `IPoller`를 만들지 않고, 여러 socket이나 monitor 전체의 준비 상태를
한 번에 기다리는 static one-shot helper.

```csharp
int ready = ZlinkPoll.Poll(new IZlinkSocket[] { dealer, sub }, timeoutMs: 1000);
```

**Options.** 4개 overload: `Poll(IReadOnlyList<IZlinkSocket> sockets, int
timeoutMs)`(readable 여부만 확인); `Poll(IReadOnlyList<IZlinkSocket> sockets,
IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents, int
timeoutMs)`(socket별 요청 event, 발생한 event를 대응 인덱스의 `revents`에 씀); 그리고
socket 대신 `IReadOnlyList<ISocketMonitor>`를 받는 같은 두 overload. 음수
`timeoutMs`는 무기한 block한다.

**Completion result.** 동기다 — 각 overload는 준비된 source 개수를 반환한다
(timeout이면 `0`).

**선택 기준.** 작고 고정된 socket/monitor 집합에 대한 임시 one-off wait엔
`ZlinkPoll.Poll`을, 감시 대상 집합이 시간에 따라 바뀌거나 timer를 socket과 함께
multiplex해야 할 땐 대신 `IPoller`를 쓴다.

---

## Eventing enum

위 모든 항목에서 참조하는 공유 enum.

| Enum | 사용처 | 값 |
|---|---|---|
| `SocketEvent`(`[Flags]`) | `ISocket.MonitorOpen(SocketEvent)`(Sockets category) | `Connected`, `ConnectDelayed`, `ConnectRetried`, `Listening`, `BindFailed`, `Accepted`, `AcceptFailed`, `Closed`, `CloseFailed`, `Disconnected`, `MonitorStopped`, `HandshakeFailedNoDetail`, `ConnectionReady`, `HandshakeFailedProtocol`, `HandshakeFailedAuth`, `PeerWeightChanged`, `All` |
| `MonitorEventType` | `MonitorEvent.Event` | `SocketEvent`의 lifecycle 값을 그대로 반영(`All` 제외) |
| `MonitorSourceKind` | `MonitorStatus.SourceKind` | `Socket` |
| `MonitorStateFlags`(`[Flags]`, `uint`) | `MonitorStatus.StateFlags` | `None`, `Ready`, `BoundReady`, `Closed` |
| `MonitorStatusDetailFlags`(`[Flags]`, `uint`) | `MonitorStatus.DetailFlags` | `None`, `SendPendingMessages`, `ReceivePendingMessages`, `AutoHwmBudget`, `AutoHwmBuffers` |
| `AutoHwmRecalcReason`(`uint`) | `MonitorStatus.AutoHwmLastRecalcReason` | `None`, `Initial`, `RoleChange`, `PolicyToggle`, `Refresh`, `DeferredShrink` |
| `PollSourceKind` | `PollEvent.SourceKind` | `Socket`, `Fd`, `Timer` |
| `PollEventFlags` | `IPoller.Add`/`Modify`/`Wait`, `ZlinkPoll.Poll` | `None`, `PollIn`, `PollOut`, `PollErr`, `PollPri`, `PollCompletion` |

---

[`Contracts/Eventing/`](../../../../bindings/dotnet/src/Zlink/Contracts/Eventing/)와
[.NET 바인딩 스펙](../../spec/dotnet/README.ko.md)에서 전체 근거를 확인한다.
