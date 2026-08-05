한국어 | [English](04-eventing.en.md)

[레퍼런스 목차](README.ko.md)

# 04. Eventing

이 category는 socket monitoring, 재사용 가능한 poller, poll-result
buffer를 다룬다 — 각각 `Socket.monitorOpen(...)`(Sockets category)와
`createPoller()`(Core category)로 생성된다. `Timer`/`AtomicCounter`/
`Stopwatch`/`Thread`는 `Timer`의 interface가 이 category의
`timer.ts` 파일에 물리적으로 선언돼 있음에도, 이 레퍼런스 트리가 따르는
언어간 관례에 따라 Core category에 문서화돼 있다. 정확한 signature는
[`contracts/eventing/`](../../../../bindings/node/src/zlink/contracts/eventing/)가
소유한다.

---

## `MonitorSocket`

socket의 connection lifecycle event를 관찰하고 현재 상태를 읽는다(이
binding의 contract에선 다른 언어의 `SocketMonitor`/`socket_monitor_t`와
달리 `MonitorSocket`으로 명명됨).

```ts
const monitor = socket.monitorOpen(SOCKET_MONITOR_EVENT_ALL);
monitor.onEvent(event => logger.info(`${event.event} ${event.remoteAddr}`));
const status = monitor.status();
```

**Options.** `recv(flags?: number)`(`MonitorEvent | null` 반환 —
non-blocking flag 아래 대기 중인 게 없으면 `null`),
`onEvent(handler: (event: MonitorEvent) => void)`, `status()`
(`MonitorStatus` 반환), `close()`.

**Completion result.** 모든 member는 동기다.

**선택 기준.** 한 번 등록하는 수동적 lifecycle observer엔 `onEvent`를
쓰거나, 생성 시점에 handler를 `Socket.monitorOpen(events, handler)`
(Sockets category)에 직접 넘긴다. pull 기반 drain loop엔 대신 `recv`를
쓴다. 시점 스냅샷엔 `status()`를 쓴다.

---

## `MonitorEvent`

monitor가 보고하는 socket connection-lifecycle event 하나. private
생성자를 가진 `class`다 — instance는 monitor `recv`/`onEvent`
operation으로만 생성되며 직접 생성할 수 없다.

**Options.** 인자 없음 — 읽기 전용 필드: `event`(`MonitorEventType`),
`value`(`number`, 에러 코드나 reconnect interval 같은 event별 값),
`routingId`(`RoutingId | null`), `localAddr`/`remoteAddr`(`string`).

**Completion result.** 해당 없음 — monitor가 전달하는 불변 값.

**선택 기준.** `event`로 분기해 특정 lifecycle transition에 반응한다.
event별 세부사항(`event`에 따라 의미가 다름)엔 `value`를 읽는다.

---

## `MonitorStatus`

`MonitorSocket.status()`가 반환하는, socket의 monitored 상태와
auto-high-water-mark telemetry 스냅샷. 순수 읽기 전용 interface다.

**Options.** 인자 없음 — 모든 member가 `readonly` property다.

| 그룹 | Member |
|---|---|
| ABI identity | `abiVersion`, `structSize`(`number`) |
| Source/state | `sourceKind`(`MonitorSourceKindValue`), `stateFlags`/`detailFlags`(`number` 비트마스크), `isReady()`(계산 method) |
| Pending count | `sndPendingMsgs`, `rcvPendingMsgs`(`bigint`) |
| Auto-HWM 설정 | `autoHwmEnabled`(`boolean`), `autoHwmProfile`/`autoHwmRole`/`autoHwmPolicyClass`(`number` — **`AutoHwmProfileValue`로 타입 지정되지 않음**, `ContextOptions.autoHwmProfile`과 달리 여기선 raw number), `autoHwmUnitBudgetBytes`/`autoHwmSocketMessageSlots`(`bigint`), `autoHwmSizeCap`(`number`) |
| Connection bucket | `autoHwmConnectionBucketEnabled`(`boolean`), `autoHwmConnectionBucketCount`/`Index`/`Hwm4K`(`number`), `autoHwmConnectionBucketHysteresisRetained`(`boolean`) |
| Auto-HWM plan(byte) | `autoHwmEffectiveMessageBytes`, `autoHwmPlannedSndHwmBytes`/`PlannedRcvHwmBytes`, `autoHwmAppliedSndHwmBytes`/`AppliedRcvHwmBytes`(`bigint`), `autoHwmEffectiveSndBuf`/`EffectiveRcvBuf`(`number`) |
| Auto-HWM recalc | `autoHwmLastRecalcMs`(`bigint`), `autoHwmLastRecalcReason`(`number`), `autoHwmSendBlockedRatioPpm`(`number`) |
| Auto-HWM deferred shrink | `autoHwmDeferredSndHwmBytes`/`DeferredRcvHwmBytes`(`bigint`, 대응하는 `autoHwmDeferredSndHwmValid`/`DeferredRcvHwmValid` `boolean`이 true일 때만 유효) |
| In-flight/과금 | `sndBytesInFlight`, `rcvBytesInFlight`, `minimumCoreMessageChargeBytes`, `oversizeMessageAdmissionCount`, `oversizeMessageAdmissionMaxBytes`(`bigint`) |

**Completion result.** 모든 property는 불변 스냅샷에 대한 동기 읽기다.

**선택 기준.** `stateFlags`를 직접 디코딩하는 대신 `isReady()`를
호출한다. socket의 실제 send/receive HWM이 설정한
`CommonSocketOptions` 값(Sockets category)과 다른 이유를 진단할 땐
connection-bucket과 auto-HWM-plan 필드를 쓴다.

---

## `Poller`

socket, file descriptor, timer를 하나의 재사용 가능한 wait로
multiplex한다.

```ts
const poller = createPoller();
poller.add(dealer, [PollEventFlag.PollIn], 1);
poller.add(timer, 2);
const events = createPollEvents(8);
const ready = poller.wait(events, 1000);
```

**Options.** `size`(읽기 전용). `add(socket: BaseSocket, events:
readonly PollEventFlagValue[], slot: number)` — **event는 varargs(java)나
결합된 bitmask(dotnet/cpp)가 아니라 `readonly` 배열 인자로 주어진다**;
`add(timer: Timer, slot: number)`. `modify(socket, events)`,
`modifyFd(fd, events)`. `remove(socket)`/`remove(timer)`/
`removeFd(fd)`(각각 `boolean` 반환, 등록돼 있었으면 true). `addFd(fd,
events, slot)`. `wait(events: PollEvents, timeoutMs: number)` — 음수
timeout은 무기한 block한다.

**Completion result.** 등록/제거 member는 동기다. `wait`는
`timeoutMs`까지 block하며, `events`를 그 자리에서 채우고 준비된 개수를
`number`로 반환한다.

**선택 기준.** 서비스 수명 전체에서 poller 하나를 쓴다. 감시하는 event만
바뀔 땐 `remove` + `add` 대신 `modify`를 선호한다. `wait` 호출마다 새로
만드는 대신 `PollEvents` buffer 하나를 재사용한다.

---

## `PollEvents` / `PollEvent`

`Poller.wait(...)`이 채우는 재사용 가능한 poll 결과 buffer로,
`createPollEvents(capacity)`(Core category)로 생성된다 — java의
`PollEvents`와 비슷한 설계이며, dotnet의 `Span<PollEvent>`/cpp의 raw
pointer-and-capacity 쌍과 구별된다.

```ts
const events = createPollEvents(16);
poller.wait(events, 500);
for (let i = 0; i < events.readyCount; i++) {
  if (events.hasEvent(i, PollEventFlag.PollIn)) { /* ... */ }
}
```

**Options.** `PollEvents`: `capacity`(읽기 전용 `number`),
`readyCount`(읽기 전용 `number`), `sourceKind(index)`/`slot(index)`/
`revents(index)`/`fd(index)`(각각 `number` 반환), `hasEvent(index,
event: PollEventFlagValue)`(편의 bit-test, `boolean`), `close()`.
`PollEvent`는 순수 읽기 전용 interface다 — `sourceKind`, `slot`,
`revents`, `fd`(전부 `number`) — `PollEvents`와 구별되며, 이 레퍼런스
tier에 문서화된 어떤 진입점으로도 직접 생성되지 않는다(java의
`PollEvents.eventAt(index)`와 달리, 이 binding의 `PollEvents`엔 대응하는
materialize 메서드가 선언돼 있지 않다).

**Completion result.** 모든 `PollEvents` accessor는 동기다.
`PollEvent`는 자신의 accessor method가 없는 순수 읽기 전용 값 형태다.

**선택 기준.** `revents(index)`를 손으로 bit-test하는 대신
`hasEvent(index, flag)`를 선호한다.

---

## Eventing 상수

| 상수 | 사용처 | 값 |
|---|---|---|
| `MonitorSourceKind` | `MonitorStatus.sourceKind` | `Socket` — 소스 자체 주석에 따르면 **"Core raw API는 socket source만 정의한다"** |
| `MonitorEventType` | `Socket.monitorOpen(events)`(Sockets category), `MonitorEvent.event` | `Connected`, `ConnectDelayed`, `ConnectRetried`, `Listening`, `BindFailed`, `Accepted`, `AcceptFailed`, `Closed`, `CloseFailed`, `Disconnected`, `MonitorStopped`, `HandshakeFailedNoDetail`, `ConnectionReady`, `HandshakeFailedProtocol`, `HandshakeFailedAuth`, `PeerWeightChanged` — 여기엔 `All` member가 없다. 대신 Sockets category의 `SOCKET_MONITOR_EVENT_ALL` 상수를 쓴다 |

---

[`contracts/eventing/`](../../../../bindings/node/src/zlink/contracts/eventing/)와
[Node 바인딩 스펙](../../spec/node/README.ko.md)에서 전체 근거를 확인한다.
