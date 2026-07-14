# Kotlin — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> Java 런타임을 공유하므로 **Kotlin 고유 표면(`suspend`·`Flow`·DSL)**의 갭만 여기 둔다. 런타임 동작 갭은 [java](java.ko.md)가 소유한다.

**이 문서는 계약이 아니라 작업 목록이다.** 계약은 spec이 소유한다. 여기서는 **스펙과 코드가 어긋난 자리**와 그것을 닫았는지만 추적한다.

**두 종류를 구분한다** — **미구현**(없다 → 만든다) / **결함**(있는데 계약과 다르게 돈다 → 동작을 바꾼다). 결함이 더 위험하다: 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **부하가 걸릴 때만 드물게 깨진다.**

## 1. 진행 체크리스트

**전체 8건. 완료 0건.**

### 언어별 표면 차이 (기준선 대조)

- [ ] **§12.3** — 근거 없는 공개 표면과 connect 상태 처리 (Java, Kotlin)
- [ ] **§12.14** — Kotlin option helper가 수신 한도를 되돌린다 (Kotlin)
- [ ] **§12.19** — typed 표면 경계 (Java, Kotlin)

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [ ] **§12.20** (결함) — 응답에 packet name을 싣는다
- [ ] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
- [ ] **§12.22** (결함+미구현) — HTTP client가 framework 계약 밖에 있다
- [ ] **§12.23** (미구현) — worker 축 분리와 `yield` 부재
- [ ] **§12.24** (결함) — actor join의 orchestration이 뒤집혀 있다

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 언어별 표면 차이 상세

### §12.3 근거 없는 공개 표면과 connect 상태 처리 (Java, Kotlin)

**계약 위반(Java).** 다음 두 표면은 공통 스펙에 근거가 없고 다른 언어에도 없다.

- connector `disconnect()` / `reconnect()` — [32 §6](../stream-connector/32-stream-connector.ko.md)의 연결 lifecycle
  표면은 connect / close / dispatch 셋뿐이며, 재연결은 자동 reconnect 옵션이 담당한다.
  **Kotlin wrapper(`ZLinkKotlinStreamConnector`)도 같은 두 메서드를 그대로 위임 노출한다.**
- **`connect()`가 진행 중인 연결 시도를 기다리지 않는다.** `Connecting`이나 `Reconnecting`
  상태에서 다시 호출하면 기존 시도를 기다리지 않고 새 연결 시도를 시작하며, 예약된 reconnect
  작업은 scheduler에 그대로 남는다. 계약은 진행 중인 시도의 결과를 기다리는 것이다
  ([32 §6](../stream-connector/32-stream-connector.ko.md)).
- `ZLinkActorPlacement(preferredNodeRid, routeMesh)` — [22 §4](../server/22-actor-model.ko.md)와
  [31 §10.2](../server/31-session-actor-dispatch.ko.md)는 remote node를 직접 지정하는 actor 생성 표면을 두지
  않는다고 규정한다.

### §12.14 Kotlin option helper가 수신 한도를 되돌린다 (Kotlin)

**미충족(Kotlin).** Kotlin의 compression option helper가 options를 복사할 때
`maxReceivedMessages`를 전달하지 않는 constructor overload를 골라, 사용자가 지정한 값을
`Integer.MAX_VALUE`로 되돌린다. wrapper는 buffering 정책을 바꾸면 안 되며 모든 option 값을
보존해야 한다([languages/java/03 §13](../stream-connector/languages/java/03-stream-connector.ko.md)).

### §12.19 typed 표면 경계 (Java, Kotlin)

**미충족.** 두 항목이다.

- Java `send(Object)`가 raw `ZLinkStreamEncodedPayload`도 그대로 받아 typed 경로에서 처리한다.
  raw payload는 raw 표면이 소유해야 한다.
- Kotlin wrapper에 목표 계약에 없는 request `await<T>()` overload 2개(typed·raw)가 있다. 목표
  선언에 없는 공개 표면은 두지 않는다.
