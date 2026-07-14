# Java — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> Kotlin adapter가 이 런타임을 공유한다. Kotlin 고유 갭은 [kotlin](kotlin.ko.md)에 있다.

**이 문서는 계약이 아니라 작업 목록이다.** 계약은 spec이 소유한다. 여기서는 **스펙과 코드가 어긋난 자리**와 그것을 닫았는지만 추적한다.

**두 종류를 구분한다** — **미구현**(없다 → 만든다) / **결함**(있는데 계약과 다르게 돈다 → 동작을 바꾼다). 결함이 더 위험하다: 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **부하가 걸릴 때만 드물게 깨진다.**

## 1. 진행 체크리스트

**전체 33건. 완료 0건.**

### 구현 감사에서 발굴 (2026-07-14, 스펙↔코드 직접 대조)

- [ ] **IMP-JV-01** (결함) — 40 §2.1
- [ ] **IMP-JV-02** (결함) — 24 §3·§5
- [ ] **IMP-JV-03** (결함) — 54 §6
- [ ] **IMP-JV-04** (결함) — 24 §4.1·05 §2.3
- [ ] **IMP-JV-05** (결함) — 20 §8
- [ ] **IMP-JV-06** (결함) — 05 §2.x
- [ ] **IMP-JV-07** (미구현) — 54 §9
- [ ] **IMP-JV-08** (미구현) — 40 §9
- [ ] **IMP-JV-09** (결함) — 40 §2.3
- [ ] **IMP-JV-10** (미구현) — 54 §3.4

### 교차 언어 결함 (여러 구현에 같은 문제)

- [ ] **IMP-X1** — pending actor row(`ActorRef` 비어 있음)를 resolve 성공으로 반환한다
- [ ] **IMP-X2** — location event source(`location-peer/spot/actor/route`, `StoreFailure`/`StoreRecovered`)가 없다
- [ ] **IMP-X3** — startup validation이 스펙의 설정 오류를 통과시킨다
- [ ] **IMP-X4** — location store read에 5초 취소 상한이 없다

### 언어별 표면 차이 (기준선 대조)

- [ ] **§12.1** — STREAM connector 수신 큐 overflow (Java)
- [ ] **§12.2** — actor join admission이 선택 사항 (Java, C++)
- [ ] **§12.3** — 근거 없는 공개 표면과 connect 상태 처리 (Java, Kotlin)
- [ ] **§12.4** — connector 호출별 packet name override (Java)
- [ ] **§12.8** — monitoring 표면 (Java)
- [ ] **§12.9** — spot 전송 표면에 channel 이름을 함께 받는다 (Java)
- [ ] **§12.10** — connector transport enum 부재 (Java)
- [ ] **§12.12** — connector dispatch mode 이름 (Java)
- [ ] **§12.13** — connector inbound observer option 부재 (Java)
- [ ] **§12.15** — 예외 정규화 부재 (Java)
- [ ] **§12.16** — metadata 총 크기 한도 미검사 (Java)
- [ ] **§12.17** — correlated Error 처리 (Java)
- [ ] **§12.18** — flow_id 미전파 (Java)
- [ ] **§12.19** — typed 표면 경계 (Java, Kotlin)

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [ ] **§12.20** (결함) — 응답에 packet name을 싣는다
- [ ] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
- [ ] **§12.22** (결함+미구현) — HTTP client가 framework 계약 밖에 있다
- [ ] **§12.23** (미구현) — worker 축 분리와 `yield` 부재
- [ ] **§12.24** (결함) — actor join의 orchestration이 뒤집혀 있다

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 구현 감사 상세

| ID | 종류 | 계약 | 구현이 하는 일 |
|----|------|------|----------------|
| **IMP-JV-01** | 결함 | [40 §2.1](../server/40-location-runtime.ko.md): actor type마다 `actor:<type>` **capability**를 기록하고, handoff는 **정확히 일치하는** capability를 가진 노드만 고른다. **application metadata로 대신 기록하지 않는다** | `ZLinkLocationAutoConnectHost.java:26-27,66-70` — `metadata["zlink.framework.actor-host"]="true"` **불리언 하나**뿐이고 `capabilities`는 **항상 `null`**(:144-146). `ZLinkFrameworkRuntime.java:566-575`는 **actor type을 보지 않는다.** ⇒ `Warrior`만 만들 줄 아는 노드에 `Mage`를 넘겨 **drain 중 actor 유실**. 게다가 stream node가 있으면 그 플래그를 아예 안 써서, 두 노드가 모두 stream을 호스팅하는 흔한 구성에선 **handoff 대상이 0** |
| **IMP-JV-02** | 결함 | [24 §3·§5](../server/24-spot-address-messaging.ko.md): **정상 전송 경로는 store를 읽지 않는다.** handle이 snapshot을 들고, stale 실패 시 **1회 갱신 + 1회 재전송** | `FrameworkSpotHandle.java:6` — `record FrameworkSpotHandle(RoutingId spotRid)`, **rid 하나뿐**. snapshot도 swap도 없다. 그래서 `ZLinkChannelSpotCalls.java:128,200` 등 **모든 spot 전송이 매번 store를 읽는다.** ⇒ 룸 핫패스마다 Redis 왕복, store 장애 시 라우트 소켓이 멀쩡해도 **모든 spot 전송 실패**(스펙은 fail-static 요구) |
| **IMP-JV-03** | 결함 | [54 §6](../server/54-graceful-drain-handoff.ko.md): **호출자의 취소는 그 호출자의 대기만 중단한다** | `ZLinkFrameworkRuntime.java:72-74,463-465` — `drain()`/`awaitDrained()`가 **런타임 내부 `CompletableFuture`를 그대로 반환**한다. 호출자가 `orTimeout(5s)`를 걸면 **런타임의 공유 종료 future가 예외로 완료**되고, 실제 drain이 끝나도 lifecycle 훅이 예외를 받는다 |
| **IMP-JV-04** | 결함 | [24 §4.1](../server/24-spot-address-messaging.ko.md)·[05 §2.3](../05-framework-api.ko.md): `SpotRouteNotFound`/`RouteNotConnected`/`RequestTargetNotFound`를 구분한다 | `ZLinkChannelSpotCalls.java:228-236` — 전부 `ZLinkConfigurationException`(kind=`REQUEST_FAILED`, retriable=false). ⇒ **"spot이 사라졌다"와 "mesh가 아직 수렴 중이다"를 구분할 수 없다.** retriable 기반 재시도 정책이 영영 안 돈다 |
| **IMP-JV-05** | 결함 | [20 §8](../server/20-spot-messaging.ko.md): router/pub-sub 미설정, bind endpoint 없음, route bridge 대상 없음은 **설정 오류** | `SpotNodeRegistration.java:220-238` — 셋 다 검사하지 않는다. `enableRouter()`가 bind 없이도 통과(:106-108)하고, `ZLinkLocationAutoConnectHost.java:79`가 **빈 endpoint peer row를 조용히 게시**한다. ⇒ 아무도 dial하지 못하는 노드가 정상 기동하고, remote join·transfer·handoff가 **말없이 전부 실패** |
| **IMP-JV-06** | 결함 | [05 §2.x](../05-framework-api.ko.md): 없는 것을 있는 척하지 않는다 | `ZLinkSendCall`/`ZLinkRequestCall`/`ZLinkPublishCall`의 `metadata(k,v)` — 스펙에 없는 표면인데다 **구현 11곳이 전부 인자를 버리고 `return this`**. `.metadata("tenant","acme")`가 컴파일되고 돌아가는데 수신 handler는 그 값을 **영영 못 본다** |
| **IMP-JV-07** | 미구현 | [54 §9](../server/54-graceful-drain-handoff.ko.md): `zlink.drain.state`(gauge), `zlink.drain.duration`(`outcome`), `zlink.drain.forced`(`kind`는 `actor\|spot\|request\|session`으로 **고정**) | 앞의 둘이 **없다.** `zlink.drain.forced`는 `kind=runtime`(닫힌 집합 밖)을 **한 번만** 올린다(`ZLinkFrameworkRuntime.java:652-653`) |
| **IMP-JV-08** | 미구현 | [40 §9](../server/40-location-runtime.ko.md) | location event source 5개 중 **4개가 없다**(IMP-X2) |
| **IMP-JV-09** | 결함 | [40 §2.3](../server/40-location-runtime.ko.md) | pending actor row를 성공 resolve로 반환한다(IMP-X1). ⇒ 두 노드가 claim을 경쟁하면 **actor 객체가 아직 없는 노드로 packet이 dispatch**된다 |
| **IMP-JV-10** | 미구현 | [54 §3.4](../server/54-graceful-drain-handoff.ko.md) | store read 5초 상한 없음(IMP-X4) |

## 3. 언어별 표면 차이 상세

### §12.1 STREAM connector 수신 큐 overflow (Java)

**미충족(Java).** [32 §10](../stream-connector/32-stream-connector.ko.md)은 수신 메시지 큐가 가득 차면 **새로 도착한
메시지를 버리고** `ReceivedMessageDropped`를 보고하도록 규정한다. 기본 상한은 1024다.

**근본 원인은 수신 저장소의 구조가 다르다는 것이다.** 기준선은 handler 조회와 무관한 **독립
unread-history**에 수신 메시지를 먼저 기록한다. handler 호출은 그와 별개로 진행되고, `waitFor`가
history에서 메시지를 꺼내며, `receivedCount`는 history에 남은 수를 읽는다. Java는 그런 history가
없고 **manual dispatch callback 큐**를 그 자리에 쓴다. 그래서 다음이 전부 어긋난다.

- overflow 시 **가장 오래된 항목을 버린다.** 기준선은 새로 도착한 메시지를 버린다.
- 수신 큐 기본 상한이 `Integer.MAX_VALUE`라 이 경로가 평소 발화하지 않는다.
- drop 시 오류를 발생시키지 않아 **메시지가 조용히 유실된다.** `ZLinkStreamErrorCode`에
  `RECEIVED_MESSAGE_DROPPED`가 없다.
- **등록된 handler가 없는 메시지는 보관되지 않고 즉시 버려진다.** 기준선은 history에 남긴다.
- **`waitFor`가 이미 도착한 메시지를 소비하지 못한다.** `submit()` 시점에 일회성 handler를 걸기
  때문에 그 이전에 온 메시지는 영영 못 받는다.
- **`receivedCount`의 의미가 다르다.** unread-history의 메시지 수가 아니라 manual 큐에 남은
  callback 수다.
- **`AUTO`(= `Immediate`) 모드에서는 큐 자체를 쓰지 않아** 수신 한도가 적용되지 않는다.

독립 unread-history를 도입해야 위 항목이 함께 해소된다.

### §12.2 actor join admission이 선택 사항 (Java, C++)

**미충족(Java, C++).** [22 §8](../server/22-actor-model.ko.md)과 [23 §12](../server/23-spot-actor.ko.md)는 actor join
admission을 **필수 등록 축**으로 규정한다. `.NET`은 이를 default 구현 없는 interface member로 두어
구현 누락 자체가 불가능하다.

Java는 `onActorJoin`에 default 구현이 있고 그 기본값이 **거절**이다. C++은 duck typing으로 존재할
때만 호출하며, 일반 spot에서 없으면 **거절**로 대체한다. 두 경우 모두 admission을 빠뜨리면
컴파일과 시작은 통과하고 **모든 actor join이 조용히 거절**되는 실패 모드가 생긴다.

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

### §12.4 connector 호출별 packet name override (Java)

**미충족(Java).** [32 §5](../stream-connector/32-stream-connector.ko.md)는 호출자가 명시한 packet name이 타입 기반
기본 이름보다 우선한다고 규정한다. Java connector의 send/request call에는 `packetName(...)`이 없다.

### §12.8 monitoring 표면 (Java)

**미충족(Java).** 세 항목이다.

- runtime event 모델이 **sealed 계층이 아니라 flat record + kind enum**이다. 기준선은 event 종류마다
  필요한 payload만 필수 인자로 갖는 sealed hierarchy이며, [00 §5](../00-public-contract-governance.ko.md)의
  "같은 상태를 kind와 nullable 값 두 축으로 표현하지 않는다"에 해당한다. Java 언어 스펙이 고정한 목표
  선언(`ZLinkLocationRuntimeEvent` / `ZLinkSpotEvent` sealed interface + permitted record)을 따라야 한다.
- `ZLinkMonitoringOptions`에 `addLocationPeerEvents` / `addLocationSpotEvents` /
  `addLocationActorEvents` / `addLocationRouteEvents` 4개가 없다.
- `ZLinkRuntimeEventHandler.handle`이 `void`를 반환해 비동기 handler를 표현할 수 없다. 계약은
  `CompletionStage<Void>`다.

### §12.9 spot 전송 표면에 channel 이름을 함께 받는다 (Java)

**계약 위반(Java).** [24 §3](../server/24-spot-address-messaging.ko.md)은 "handle이 전송 mesh를 소유하므로
caller가 route channel을 함께 고르지 않는다"고 규정한다. Java `ZLinkRouteClient.sendToSpot` /
`requestToSpot`은 `(channelName, SpotHandle, message)`를 받아 caller가 mesh를 다시 고르게 만든다.
계약은 `(SpotHandle, message)`다.

### §12.10 connector transport enum 부재 (Java)

**미충족(Java).** Java 언어 스펙이 고정한 `ZLinkStreamTransport`(`TCP`/`TLS`/`WEB_SOCKET`/
`WEB_SOCKET_SECURE`)가 구현에 없다. 지원 transport 집합을 공개 계약으로 관찰할 수 없다.

### §12.12 connector dispatch mode 이름 (Java)

**미충족(Java).** [32 §7](../stream-connector/32-stream-connector.ko.md)이 고정한 dispatch mode의 닫힌 집합은
`Manual`(기본)과 `Immediate`다. Java는 `AUTO`/`MANUAL`을 쓴다. 닫힌 enum의 멤버 이름은 관측·설정
데이터의 안정 키이므로 언어마다 다를 수 없다 — close reason과 error code는 이미 공통 이름을
SNAKE_CASE로 1:1 사상하고 있어 dispatch mode만 예외인 상태다.

**이름만의 문제가 아니다.** 두 가지 동작이 더 어긋난다.

- **`MANUAL`에서도 connection-state와 disconnected callback이 dispatch queue를 우회해** lifecycle
  스레드에서 직접 실행된다. 계약은 manual mode에서 모든 사용자 callback이 `dispatch()` 호출
  문맥에서 실행되는 것이다.
- **message callback이 반환한 `CompletionStage`를 기다리지 않는다.** 그래서
  `dispatch().submit()`이 callback 완료 전에 끝난다. 기준선은 callback 완료까지 기다린다.

### §12.13 connector inbound observer option 부재 (Java)

**미충족(Java).** [32 §10](../stream-connector/32-stream-connector.ko.md)은 inbound observer 통지 큐(기본 1024개)와
payload preview 한도(기본 0바이트)를 option으로 조절한다고 규정한다. Java
`ZLinkStreamConnectorOptions`에는 `maxInboundObserverNotifications`와
`maxInboundObserverPayloadPreviewBytes`가 없어 그 한도를 관찰하거나 조절할 수 없다.

### §12.15 예외 정규화 부재 (Java)

**미충족(Java).** 기준선은 connector의 비동기 실패를 `ZLinkStreamErrorCode`를 담은 공통 예외
타입으로 정규화해, 호출자가 실패 원인을 닫힌 집합으로 판별할 수 있게 한다. Java는 raw
`TimeoutException`, `IllegalStateException`, `IllegalArgumentException`을 그대로 던져 오류 코드를
잃는다([32 §9](../stream-connector/32-stream-connector.ko.md)).

### §12.16 metadata 총 크기 한도 미검사 (Java)

**미충족(Java).** [32 §4](../stream-connector/32-stream-connector.ko.md)는 metadata 블록의 **총합 1024바이트** 한도를
규정한다. Java wire codec은 항목 수와 개별 key/value 길이만 검사하고 총합을 검사하지 않아, 한도를
넘는 프레임을 만들 수 있다.

### §12.17 correlated Error 처리 (Java)

**미충족(Java).** request sequence가 붙은 `Error` 프레임은 그 request의 완료로만 매핑해야 한다.
Java는 매핑 자체는 하지만(`pendingRequests.fail(...)`), **그 전에 stream-level error callback으로도
발행해 같은 오류가 두 번 전달된다.** 또 error payload의 JSON 객체를 파싱하지 않아 서버가 보낸 오류
상세를 잃는다.

### §12.18 flow_id 미전파 (Java)

**미충족(Java).** [53 §6](../server/53-flow-correlation.ko.md)은 inbound callback 안에서 시작한 send/request가
그 inbound의 `flow_id`를 이어받도록 규정한다. Java connector는 매 호출마다 새 UUIDv7을 만들어
flow가 경계에서 끊긴다.

### §12.19 typed 표면 경계 (Java, Kotlin)

**미충족.** 두 항목이다.

- Java `send(Object)`가 raw `ZLinkStreamEncodedPayload`도 그대로 받아 typed 경로에서 처리한다.
  raw payload는 raw 표면이 소유해야 한다.
- Kotlin wrapper에 목표 계약에 없는 request `await<T>()` overload 2개(typed·raw)가 있다. 목표
  선언에 없는 공개 표면은 두지 않는다.
