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

## 라운드 2 (2026-07-14) — 관측 · channel topology · companion 패키지

### 체크리스트

- [ ] **IMP-JV-11** (결함) — `flow_id`를 envelope header가 아닌 **자체 message part**로 나른다 (교차 언어 wire 위반)
- [ ] **IMP-JV-12** (결함) — per-source polling 간격을 **전역 최소값 하나로 붕괴**시킨다
- [ ] **IMP-JV-13** (미구현) — 계기 12개 결측(그중 `channel.messages.dropped`가 치명적)
- [ ] **IMP-JV-14** (결함) — runtime-event handler 예외를 **error sink에 보고하지 않는다**
- [ ] **IMP-JV-15** (미구현) — `fanout.published`/`received`에 `topic` 라벨이 없다
- [ ] **IMP-JV-16** (결함) — **수동 endpoint가 그 역할의 자동 연결 reconcile을 끄지 않는다**
- [ ] **IMP-JV-17** (결함) — 자동 연결 역할에 대한 런타임 `connect()`가 **거부되지 않는다**
- [ ] **IMP-JV-18** (결함) — HTTP client가 **proxy 자격증명을 대상 서버로 흘린다**
- [ ] **IMP-JV-19** (결함) — HTTP attempt timeout이 **redirect hop마다** 적용된다
- [ ] **IMP-JV-20** (결함) — connector send payload 한도를 **압축 전** payload에 적용한다

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-JV-11** | [53 §3.1·§3.4](../server/53-flow-correlation.ko.md): `flow_id`/`flow_origin`은 **envelope header의 1급 필드**다. 다르게 나르는 relay를 두지 않는다 | `ZLinkChannelFlowFrame.java:9-27` — `"__zlink.flow\n<uuid>\n<ORIGIN>"`를 **세 번째 message part**로 인코딩한다(`ZLinkChannelCallRuntime.java:215-223`). **다른 어떤 구현도 그 part를 쓰거나 읽지 않고**, Java는 header의 flow 필드를 **읽지 않는다.** ⇒ Java가 낀 흐름은 fleet 추적에서 **끊긴다** |
| **IMP-JV-12** | [50 §4](../server/50-runtime-monitoring.ko.md): polling 주기는 등록 시점에 **항상 명시**한다. **숨은 기본 주기를 두지 않는다** | `DefaultZLinkMonitoringOptions.java:61-67` — 모든 source 간격의 **최소값 하나**로 `scheduleWithFixedDelay` 하나를 돌리고, 매 tick에 **모든 source를 poll**한다. ⇒ `addSpotEvents("play",200ms)` + `addLocationRuntimeEvents("loc",60s)`면 Redis topology 페이징이 **200ms마다** 돈다 — 설정한 비용의 **300배** |
| **IMP-JV-13** | [51](../server/51-runtime-metrics.ko.md)·[52 §2](../server/52-message-flow-tracing.ko.md): **metric/counter는 trace mode와 무관하게 계속 발생한다** | 계기 12개가 없다 — `stream.session.bind.duration`, `stream.{inbound,outbound}.bytes`, `spot.timer.tick.lateness`, `actor.count`, `actor.mailbox.depth`, **`channel.messages.dropped`**, `location.peers`, `location.store.errors`, `location.owner_lease.renew.failures`, `location.write.conflicts`, **`observability.observer.overflow`**. ⇒ trace를 끄면 drop에 대한 **관측 신호가 0** |
| **IMP-JV-14** | [50 §3.2](../server/50-runtime-monitoring.ko.md): handler 예외는 **runtime error sink로 보고한다** | `ZLinkRuntimeEventDispatcher.java:38-47` — `catch { handlerFailureCount.incrementAndGet(); }`. 공개 reader가 없는 **내부 카운터**로만 남는다 |
| **IMP-JV-15** | [51 §4.4b](../server/51-runtime-metrics.ko.md): `fanout.published`/`received`에 `topic`(닫힌 집합) 라벨 | `ZLinkChannelDirectCalls.java:126` 등 전부 `Map.of()`. ⇒ **topic별 발행/수신 차이**를 계산할 수 없다 — 이 한 쌍이 존재하는 이유가 그건데 |
| **IMP-JV-16** | [10 §5.2](../server/10-channel-topology.ko.md): 같은 역할에 수동 endpoint가 **하나라도** 있으면 그 역할은 수동으로 확정되고, **자동 연결 reconcile이 돌지 않는다** | `ZLinkLocationAutoConnectHost.java:107-127` — 모든 surface에 **무조건** reconciler를 만든다. 유일한 완화는 `ConnectableSocketExecutor.connect`(:174-186)가 **문자열이 정확히 일치하는** 수동 endpoint만 건너뛰는 것. ⇒ `enableClient("tcp://10.0.0.5:5001")` + location store면 DEALER가 **store의 staging 서버들까지 물고 라운드로빈**한다 |
| **IMP-JV-17** | [10 §5.2](../server/10-channel-topology.ko.md): 자동 연결로 확정된 역할에 런타임 수동 endpoint를 추가하려 하면 **그때 거부된다** | `RuntimeEndpointConnections.java:19-30` — 검증 후 그냥 연결한다. frozen/auto 모드가 **없다**(`.NET`은 `Freeze`, C++은 `frozen` 상태를 갖는다). ⇒ **역할마다 진실의 원천이 하나**라는 불변식이 깨진다 |
| **IMP-JV-18** | [http 07 §7.3](../http-client/07-auth-tls-proxy.ko.md) | `RequestPerformer.java:160-162`가 `proxy-authorization`을 요청 헤더에 넣고, `JavaHttpClientFactory.java:27-30`은 `.authenticator(...)` 없이 `ProxySelector`만 준다. `.NET`(IMP-DN-12)과 **같은 결함** |
| **IMP-JV-19** | [http 06 §6.2](../http-client/06-redirect-retry-cookie.ko.md): timeout은 **시도(attempt)당** 적용한다 | `RequestPerformer.java:176-181` — `hop()`마다 timeout을 **새로 건다.** ⇒ `timeout(3s)` + `followRedirects(5)` + `retry(2)`가 계약상 ~9초여야 하는데 **~45초**를 태울 수 있다 |
| **IMP-JV-20** | [32 §4.7](../stream-connector/32-stream-connector.ko.md) | `ZLinkStreamConnectorPayloadCodec.java:23-35` — 압축 전 크기로 한도를 검사한다. `.NET`(IMP-DN-13)과 **같은 결함** |

## 라운드 3 (2026-07-14) — 근거 없는 표면 · 조용한 no-op · 경합

**Java에는 `module-info.java`가 없다.** 그래서 `zlink-framework-core`의 **모든 `public` 클래스가
application API**다. 이 사실이 아래 여러 항목의 근본이다.

### 체크리스트

- [ ] **IMP-JV-21** (결함) — `systems.zlink.framework.execution` 패키지가 **framework 내부 실행기를 공개**한다
- [ ] **IMP-JV-22** (결함) — raw STREAM frame/header codec이 core의 **public API**다
- [ ] **IMP-JV-23** (결함) — header decode 실패를 **날조한 packet으로 바꾸고**, 그 요청에 **응답할 수 없게** 만든다
- [ ] **IMP-JV-24** (결함) — Spring host 자동 drain이 **25초** — 스펙은 30초
- [ ] **IMP-JV-25** (결함) — `addForwardedMetadataKey(...)`가 **조용한 no-op**
- [ ] **IMP-JV-26** (결함) — connector가 사용자 **error callback의 실패를 삼킨다**
- [ ] **IMP-JV-27** (결함) — `includeNativeDiagnostics`를 **읽는 곳이 없다**
- [ ] **IMP-JV-28** (결함) — `ZLinkStoreSpotHandleResolver`가 **내부 transport 주소 타입을 공개 표면으로 흘린다**
- [ ] **IMP-JV-29** (결함) — connector의 `ZLinkStreamJson`·`ZLinkStreamCompressionCodecs`가 **스펙 근거가 없다**
- [ ] **IMP-JV-30** (결함) — **actor가 든 spot을 닫을 수 있다** (`.NET` IMP-DN-17과 동형)
- [ ] **IMP-JV-31** (결함) — 서버가 `correlation_id`를 `request_seq`로 **날조한다**
- [ ] **IMP-JV-32** (결함) — `listPageSize`를 **읽는 곳이 없다.** 내부 기본값이 1000이 아니라 **무한**이다
- [ ] **IMP-JV-33** (미구현) — `storeFailureGrace`를 **읽는 곳이 없다.** fail-static 유예 정책 자체가 없다

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-JV-21** | [25 §3](../server/25-stage-wrapper-on-spot.ko.md): **사용자에게 내부 실행기(mailbox·queue·drain loop)를 노출하지 않는다.** 사용자가 보는 것은 등록 표면뿐이다 | `execution/ZLinkSpotDispatchQueue.java:33,42,51,61`(public 생성자 + spot 직렬 줄에 `enqueue`), `ZLinkAsyncSerialQueue.java`, `ZLinkWorkerPool.java:21,30,77,97,125`(public 생성자 + `execute` + **`close()`**). 스펙 어디에도 이 이름들이 없다. ⇒ 앱이 **프로세스 전체 spot이 공유하는 worker pool을 `close()`할 수 있고**, spot의 turn 큐에 **임의 작업을 직접 밀어 넣어** turn 모델을 통째로 우회할 수 있다 |
| **IMP-JV-22** | [32 §5](../stream-connector/32-stream-connector.ko.md): **임의 header bytes를 다루는 API를 공개 표면에 두지 않는다** | `runtime/streams/ZLinkStreamFrameCodec.java:12,20`·`ZLinkStreamHeaderCodec.java:29,118`이 public이다. **connector는 제대로 한다**(`ZLinkStreamWireProtocol.java:10`이 package-private). core만 뚫려 있다 |
| **IMP-JV-23** | [11 §3.1](../server/11-channel-messaging.ko.md): 잘못된 frame은 **로그 + drop**. [51 §4.4](../server/51-runtime-metrics.ko.md): `dropped{reason="decode_error"}` | `runtime/spots/ActorPacketFrames.java:21-37` — header decode가 실패하면 **catch해서 raw header 바이트를 UTF-8로 읽은 값을 packet name으로 삼는 Header를 날조한다.** plain-header 경로는 이미 `decodeOrPlain`이 처리하므로, 이 catch는 **진짜로 손상된 frame에서만** 튄다. drop도 metric도 없다. 게다가 `streamHeader=false`라서 `encodeReply`가 **응답 헤더 없는 맨 payload**를 내보낸다 — 호출자의 connector는 header의 `request_seq`로 매칭하므로 **그 요청은 30초 timeout까지 매달린다** |
| **IMP-JV-24** | [54 §6](../server/54-graceful-drain-handoff.ko.md): 기본 deadline은 **모든 언어에서 30초**다. **인자 없는 overload와 host 자동 drain이 같은 값을 쓴다** | `ZLinkFrameworkRuntime.java:430-431`은 30초로 맞는데, **실제로 프로세스 종료에 도는 유일한 경로**인 `ZLinkFrameworkLifecycle.java:37,89,111`이 **25초**다. ⇒ 25~30초에 끝나는 drain이 Java에서만 `ForceStopped(DeadlineExceeded)`가 되고 다른 언어에선 `Drained`가 된다 |
| **IMP-JV-25** | 스펙이 선언한 metadata 전달 정책 | `ZLinkMetadataPolicyRegistration.java:10,17,20` — `forwardedApplicationKeys`의 **소비자가 트리 전체에 없다**(getter round-trip 유닛테스트뿐). `configureMetadata().addForwardedMetadataKey("tenant")`가 **아무것도 전달하지 않는다.** 기록된 `metadata(k,v)` no-op(IMP-JV-06)과 **같은 병**이 설정 축에서 반복된다 |
| **IMP-JV-26** | [32 §9](../stream-connector/32-stream-connector.ko.md): 사용자 callback 실패는 `UserCallbackFailed`. **error handler에 예외 조항이 없다** | `DefaultZLinkStreamConnector.java:354-361` — `catch (Throwable ignored) {}`. 바로 10줄 위 메시지 경로는 `publishUserCallbackFailed`를 **제대로 부른다.** ⇒ `onErrorReceived`가 던지면 **오류도 metric도 로그도 없다** |
| **IMP-JV-27** | — | `ZLinkDispatchOptionsRegistration.java:160,179,219`가 전부. 형제 옵션(`includeMessageSizes`·`sampleRate`·`logFile`)은 살아 있는데 이것만 죽었다 |
| **IMP-JV-28** | [00 §5](../00-public-contract-governance.ko.md): transport 주소는 framework 내부다 | `spots/ZLinkStoreSpotHandleResolver.java:10-11,34`가 **사용자 대면 `framework.spots` 패키지에서 public**이고 `runtime.internal.spots.SpotTransportAddress`를 반환한다. **코드베이스 자신이 그 타입을 `runtime/internal/` 아래 둔다** |
| **IMP-JV-29** | [00 §3](../00-public-contract-governance.ko.md): 스펙 근거 없이 public API를 만들지 않는다 | connector의 `ZLinkStreamJson`·`ZLinkStreamCompressionCodecs` — 스펙 트리 grep **0건**(형제 connector 타입은 전부 항목이 있다). `ZLinkStreamJson`은 고정된 `send`/`request`/`on` 표면을 **중복하는 두 번째 static facade**다 |
| **IMP-JV-30** | [21 §close](../server/21-spot-node.ko.md) | `ZLinkSpotLifecycle.java:134-142` — `hasActorsInSpot()`이 **락 없이** actor registry를 순회하고, `joinedSpotRid`를 **쓰는** commit은 spot dispatch 줄에서 돈다. `.NET` IMP-DN-17과 **같은 경합** |
| **IMP-JV-31** | [52 §9](../server/52-message-flow-tracing.ko.md) | `ZLinkStreamRuntime.java:272-273` — `.orElseGet(() -> requestSequence()...)`. `.NET` IMP-DN-09과 **같은 결함**(C++만 올바르다) |
| **IMP-JV-32** | [40 §3·§8.2](../server/40-location-runtime.ko.md): 목록 조회는 `list page size`(기본 **1000**)를 따른다 | `ZLinkLocationOptions.java:12,40-48` — **읽는 곳 0.** 내부 조회가 `ZLinkPageRequest.firstPage()`(pageSize 0)를 써서 Redis `SMEMBERS`로 **kind 인덱스 전체**를 읽는다. ⇒ 모든 `listSpots`/`listActors`가 **O(N) 전체 읽기**이고, 그걸 제한하라는 옵션이 **아무 일도 안 한다** |
| **IMP-JV-33** | [40 §6.1·§8.2](../server/40-location-runtime.ko.md): store 장애 유예 30초 | **읽는 곳 0.** ⇒ Java e2e의 `SF-B2 GraceExceeded`가 **존재하지 않는 정책을 검증하고 있다** |

## 교차 언어 결함 — 이 언어에서 무엇을 고치나

**교차 언어 결함이라도 고치는 일은 이 언어에서 한다.** [갭 인덱스](../90-implementation-gap.ko.md) §15.3이
**왜**(계약과 결정)를 소유하고, 아래 표가 **무엇을**(이 언어의 작업)을 소유한다.

| 교차 결함 | 무엇이 깨지나 | 이 언어의 작업 |
|---|---|---|
| **IMP-X1** | pending actor row를 resolve 성공으로 반환 | IMP-JV-09 |
| **IMP-X2** | location event source 4종 결측 | IMP-JV-08 |
| **IMP-X3** | startup validation이 설정 오류를 통과 | IMP-JV-05 |
| **IMP-X4** | location store read에 5초 상한 없음 | **이 언어 전용 ID 없음** — `runtime/locations/`(`ZLinkStoreLocationResolvers`·`ZLinkLiveLocationRows`·`ZLinkOwnerLeaseTracker`·`ZLinkAutoConnectLoop`)가 store를 **무제한**으로 호출한다. 5초 취소 상한을 적용한다 |
| **IMP-X5** | message-flow 관측자가 로그 모드에 묶여 침묵 | **이 언어 전용 ID 없음** — `ZLinkMessageFlowTracer.java:65-78`의 `enabled()`가 **로그 모드만** 읽고, 샘플 게이트까지 통과해야 :89의 관측자 dispatch에 닿는다. [52 §3](../server/52-message-flow-tracing.ko.md)은 "관측자는 모드와 무관하게 발화한다"이다. `.NET`(`ZLinkMessageFlowTracer.cs:44`)처럼 `ShouldLog(outcome) || ObserverEnabled`로 고친다 |
| **IMP-X6** | `origin=lifecycle`을 생성하지 않는다 | **이 언어 전용 ID 없음** — `ZLinkMessageFlowTracer.java:119-123`의 `originFor()`가 `RECEIVED`가 아닌 모든 것을 `APPLICATION`으로 매핑한다. enum은 wire 디코더(`ZLinkStreamHeaderCodec.java:243`)에만 있다. drain·startup·shutdown이 새 flow를 `lifecycle`로 시작해야 한다 |
| **IMP-X7** | connector send payload 한도를 압축 전에 적용 | IMP-JV-20 |
| **IMP-X8** | 수동 endpoint가 auto-reconcile을 끄지 않는다 | IMP-JV-16 |
| **IMP-X9** | HTTP client proxy 자격증명 유출 | IMP-JV-18 |
| **IMP-X12** | actor가 든 spot을 닫을 수 있다 (경합) | IMP-JV-30 |
| **IMP-X13** | `correlation_id` 날조 | IMP-JV-31 |
| **IMP-X14** | `listPageSize`가 죽어 있다 | IMP-JV-32 |
| **IMP-X15** | `storeFailureGrace`가 죽어 있다 | IMP-JV-33 |
| **IMP-X16** | `includeNativeDiagnostics`가 죽어 있다 | IMP-JV-27 |
| **IMP-X18** | Redis fixture 불일치 | `putInstant`가 null instant에 `1970-01-01T00:00:00Z`를 낸다 — fixture는 `0001-01-01T00:00:00+00:00` |

## 이전 기록 — 기준선 대조 (2026-07-13 이전)

> **이 절은 과거 기록이다.** 당시 계약 기준으로 확인한 내용이며, 그 뒤 계약이 바뀐 항목이 있다
> (특히 실행 terminator — [갭 인덱스 §12.21](../90-implementation-gap.ko.md) 참조).
> **현재 작업 목록은 이 문서 위쪽의 체크리스트다.**

### 3.1 handler 비동기 완료

Java request, send, publish, Spot, actor와 session handler는 `CompletionStage<T>` 또는
`CompletionStage<Void>`를 반환한다.

> **turn 의미는 갭이다.** 현재 구현의 automatic turn은 handler가 stage를 **반환할 때까지**만 다음
> handler의 시작을 막고, 반환된 incomplete stage의 **완료는 기다리지 않는다.** 정본 계약은
> `async`가 **완료까지 turn을 유지**하는 것이다([04 §1.1](../04-async-execution-policy.ko.md)).
> 아래 근거는 **폐기된 계약 기준의 기록**이며, 현재 갭은
> [§12.21](#1221-yield-terminator-부재-전-언어)이 소유한다.

확인 근거(구 계약 기준):

- `JavaTargetContractGapTest.handlersFactoriesAndLifecycleExposeCompletionStages`
- Config 8 `AutomaticTurnDispatch` 전체 selector — 이 config는 [config-8 실행 turn과
  terminator](../../common/e2e/config-8-execution-turn.ko.md)(`TD-*`)로 대체됐다

Kotlin adapter는 lifecycle과 actor callback의 coroutine을 `CoroutineScope.future`로
`CompletionStage`에 연결한다. `CompletionStage.await()`는
`suspendCancellableCoroutine`과 stage 완료 callback으로 coroutine을 재개하므로 callback
실행 줄을 blocking wait로 점유하지 않는다. waiter cancellation은 공유 framework stage를
취소하지 않고, stage의 완료 오류는 원래 원인으로 풀어서 전달한다.

현재 확인 위치:

- `zlink-framework-kotlin/.../ZLinkSuspendingHandlers.kt`
- `zlink-framework-kotlin/.../ZLinkCoroutineTurnAwait.kt`

### 3.2 one-way call 완료 표면

`ZLinkSendCall`, `ZLinkSessionSendCall`, `ZLinkSessionReplyCall`과
`ZLinkBoundSessionSendCall`의 one-way `submit()`은 `void`다. `ZLinkSubmitStage`, public
`await`와 yield call은 production source에 없다. 전송 실패는 framework error observer와
runtime 진단 경로로 보고한다.

### 3.3 typed session handler

`ZLinkTypedSessionPacketHandler`는 raw application handler를 상속하지 않는다. message type
descriptor와 typed `CompletionStage<Void> handle(...)`을 제공하며 framework dispatcher와
application handler의 등록 경계가 분리되어 있다.

### 3.4 Actor join 계약

`ZLinkActorContext.joinSpot(...)`과 `joinEntrySpot(...)`은 요청을 필수로 받는다. 요청 없는
overload와 default throw는 없으며, 단일 `ZLinkActorJoinCall`과 sealed 승인·거절 결과를 사용한다.

### 3.5 interface inventory 문서 상태

다음 타입은 기존 Java interface catalog에서 찾기 어려웠으며 현재 언어별 interface
inventory에 정식 public contract로 반영했다.

```text
ActorSpotHandleResolver
ManualEndpointListBuilder
SpotHandleResolver
ZLinkActorClient
ZLinkActorDirectory
ZLinkActorJoinCall
ZLinkActorLocationStore
ZLinkActorRequestCall
ZLinkActorSendCall
ZLinkChannelRuntimeOptions
ZLinkClientServerChannelRuntimeOptions
ZLinkCodecRegistrar
ZLinkLocationChangeStampStore
ZLinkLocationKey
ZLinkLocationReadiness
ZLinkLocationRuntimeQuery
ZLinkLocationStore
ZLinkLocationWatchStore
ZLinkOwnerLeaseStore
ZLinkPeerLocationResolver
ZLinkPeerLocationStore
ZLinkRouteLocationStore
ZLinkSocketRuntimeOptions
ZLinkSpotActorLifecycle
ZLinkSpotLocationStore
ZLinkSpotPacketHandler
ZLinkSpotRequestHandler
ZLinkSpotSubscriptionHandler
ZLinkSpotTimerHandler
ZLinkStreamCompressionBuilder
ZLinkTypedSessionPacketHandler
```

Kotlin 전용 public type과 top-level extension도 Kotlin interface catalog의 type 및
function inventory에 반영했다.

```text
ZLinkCoroutineSuspendHandlerInvoker
ZLinkKotlinLifecycleCall
ZLinkKotlinSendCall
ZLinkKotlinStreamConnector
ZLinkStreamTypedWaitCall
ZLinkSuspendingLocationStore
await
awaitJoinReply
awaitOwnerLeases
send
publishToTopic
resolveActorSpotHandle
resolveSpotHandle
useCoroutineHandlers
messages
errors
```

### 3.6 Actor membership와 join 결과

현재 actor context는 nullable Spot 식별자와 join boolean을 따로 노출한다. 두 값을
순서대로 읽는 동안 상태가 바뀌거나 구현이 서로 다른 값을 돌려주면 모순이 생긴다.
목표 계약은 nullable Spot 식별자 하나를 join 상태의 단일 기준으로 사용한다.

현재 join 결과도 result code 또는 승인 여부와 nullable actor를 독립 필드로 제공한다.
목표 계약은 sealed 승인/거절 결과로 바꾼다. 승인 결과만 필수 actor ref를 가지며 두
결과 모두 reply를 가진다. Kotlin은 Java sealed 계약을 그대로 사용한다.

location store/query, compression과 connector에 선언된 Kotlin public extension은 Kotlin
문서의 전체 function inventory를 기준으로 별도 검증한다. Java 완료 판정이 Kotlin 완료를
의미하지 않는다.

### 3.7 Java/Kotlin 검증 상태

Java target public declaration은 `JavaTargetContractGapTest` 전체 통과와 production symbol
검색으로 확인했다. Java Config 1~10과 Config 11 `ObservabilityOps` 전체 selector가 real E2E를
통과했다. `ZLinkMessageFlowTracerTest.dispatchErrorsUseContractLogLevels`는 handler 예외를
one-way 여부와 관계없이 Error로 기록하고, handler 없음·decode 실패·invalid frame의 기본 수준을
send는 Warning, publish는 Debug로 기록하는 계약을 고정한다. Kotlin channel handler도 같은
Java dispatch reporter를 사용한다.

Kotlin은 `KotlinPublicSurfaceContractTest`, 전체 unit/integration test와 언어별 E2E로 확인했다.
`KotlinFlowContextBridgeTest`는 suspending lifecycle의 flow가 suspension 전후에 유지되고 다음
호출에 남지 않는지 검증한다. `KotlinCompletionStageAwaitIntegrationTest`는 drain waiter를 취소해도
공유 drain stage가 취소되지 않는지 검증한다. Config 8 전체 실행은 **구 계약(`ATD-*`) 기준** 기록이며
pending await 중 Play 재시작 같은 routing id recovery를 포함해 통과했다. 그 config는
[config-8 실행 turn과 terminator](../../common/e2e/config-8-execution-turn.ko.md)(`TD-*`)로 대체됐다.
Config 11 전체 실행도 각 selector를
새 Redis와 새 토폴로지에서 실행하여 OBS-A1~C5가 모두 통과했다.

## 라운드 4 (2026-07-14) — 샘플 · E2E

### 체크리스트

- [ ] **SMP-JV-01** (**절대 규칙 위반**) — TicTacToe 밖 샘플이 **수동 연결을 쓴다**(29곳). `.NET`·Node는 0
- [ ] **SMP-JV-02** (미구현) — GameQuest에 **owner Spot이 아예 없다.** 소유권을 클라이언트 해시로 흉내낸다
- [ ] **SMP-JV-03** (미구현) — GameQuest가 **event sourcing이 아니다.** "rehydrate" 게이트를 카운터로 통과한다
- [ ] **SMP-JV-04** (미구현) — Bingo의 정본 `yield` 사용처가 코드에 없다
- [ ] **SMP-JV-05** (결함) — TicTacToe가 **MessagePack**을 쓴다 — 문서는 JSON으로 고정
- [ ] **SMP-JV-06** (결함) — publish 메시지를 `Msg`로 잘못 이름 붙였다. 올바른 `Event`는 **선언만 되고 죽어 있다**
- [ ] **SMP-JV-07** (결함) — DeliveryDispatch에 **문서에 없는 죽은 `CourierGateway` 프로세스**가 있고, Java가 **actor relay를 건너뛴다**
- [ ] **SMP-JV-08** (결함) — Bingo·DeliveryDispatch가 여전히 **환경변수·JVM system property**를 읽는다
- [ ] **SMP-JV-09** (결함) — 클라이언트 self-check가 문서보다 약하다(릴리즈 게이트)
- [ ] **SMP-JV-10** (결함) — ShoppingMall `GetOrderStateReq`가 **읽기 전용이어야 하는데 read model을 재구축**한다
- [ ] **E2E-JV-01** (결함) — `ObservabilityOps`가 **역할 서버도 클라이언트도 없이** 폐기된 config-8 바이너리를 빌려 쓴다
- [ ] **E2E-JV-02** (결함) — Config 2 커버리지 구멍(`SM-F3` 누락), 문서에 없는 `SM-Q9`
- [ ] **E2E-JV-03** (결함) — 클라이언트 21개 중 **19개가 raw `java.net.http.HttpClient`**를 쓴다
- [ ] **E2E-JV-04** (결함) — 앱 코드의 **환경변수 읽기 535곳**인데 feature-map에 기록 **0**
- [ ] **E2E-JV-05** (결함) — readiness가 최대 **20배**(60초). `ROUTE_SETTLE`이 **Java runner 전부에 없다**
- [ ] **E2E-JV-06** (결함) — 시나리오 파일이 **12줄 껍데기**이고 본문이 532줄 god-context에 있다
- [ ] **E2E-JV-07** (결함) — **`SF-B2`가 `SF-B1`과 구별되는 것을 아무것도 단언하지 않고**, 죽은 옵션으로 시간을 잰다
- [ ] **E2E-JV-08** (결함) — `feature-map` 누락, `YieldDispatch`에 **`run_e2e.sh`가 없어** 실행 불가

### 가장 무거운 것

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-JV-01** | [샘플 규약](../../common/sample/README.ko.md)의 **절대 규칙**: TicTacToe만 수동 연결을 쓸 수 있다. *"위반이 하나라도 있으면 해당 샘플 변경은 완료된 것으로 판단하지 않는다"* | Bingo·SupportChat·DeliveryDispatch·ShoppingMall·GameQuest에 `connectRouter`/`connectPeerPub`가 **29곳**(`.NET`·Node는 **0**). 인자 없는 `.enableClient()` overload가 **같은 파일에서 이미 쓰이고 있다** — 피할 수 있는 호출들이다. **[갭 인덱스 §13.2]가 "연결 축은 규약과 일치한다"고 적고 있었는데 거짓이었고, 정정했다** |
| **SMP-JV-02** | [GameQuest §1](../../common/sample/event/gamequest.ko.md): 이 샘플의 존재 이유가 **`PlayerId`별 owner spot을 노드에 분산**하는 것이다 | `addSpotMesh` **0건**(Java·Kotlin 모두). 소유권을 **클라이언트 측 해시**로 흉내낸다(`SampleTopology.java:42-47`). `.NET`엔 `QuestMission`·`GameApi` 양쪽에 있다 |
| **E2E-JV-07** | [config-6 SF-B2](../../common/e2e/config-6-store-failure-recovery.ko.md): 유예가 지나면 **새 outbound connect가 멈춘다**(장애 중 재시작한 provider를 store 복구 전에 dial하면 안 된다) | 두 언어 모두 요청을 `grace + 2×heartbeat` 동안 돌리고 unhealthy 상태를 기다릴 뿐, **장애 중 provider를 재시작하지도, 새 connect가 억제되는지 단언하지도 않는다.** SF-B1이 이미 SF-B2가 보는 것을 전부 본다. 게다가 그 시간을 **[IMP-JV-33]으로 읽는 곳이 0인 `storeFailureGrace`**에서 계산한다 — **죽은 옵션으로 시간을 재는 시나리오다** |

## 라운드 5 (2026-07-14) — GameQuest 심층

**얕은 패스는 "owner Spot이 없다"까지만 봤다. 깊이 파니 샘플 전체가 전제를 구현하지 않았다.**

Java와 Kotlin GameQuest는 **같은 코드베이스의 두 문법**이다. Spot도, spot-mesh도, event
sourcing도, location-store binding도, 자동 연결도 **없다.** 있는 것은 프로세스 전역 `HashMap` 위에
얹은 2-shard request/reply 서비스와, **쓰기만 하고 읽지 않는** Redis 감사 로그다.

**그리고 self-check의 가장 중요한 게이트 5개가 구조적으로 실패할 수 없다.**

### 진짜 버그

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-JV-11** (**버그**) | [gamequest §1·§8](../../common/sample/event/gamequest.ko.md): 이 샘플의 존재 이유가 **`PlayerId`별 owner spot을 spot-mesh에 분산**하는 것이다 | GameQuest 트리 전체에 `Spot`·`Actor` grep **0건**(Bingo·TicTacToe·SupportChat엔 다 있다). `QuestMission/Program.java:69-72`는 **channel 서버 하나**만 등록하고, 상태는 Spring 싱글턴 `QuestStore`의 `HashMap`에 `synchronized` 하나로 지킨다. ⇒ **서로 다른 player의 이벤트가 전역 모니터 하나에 직렬화된다.** owner도, lease도, re-home도 없다 |
| **SMP-JV-12** (**버그**) | [gamequest §14](../../common/sample/event/gamequest.ko.md): rehydrate는 **노드 재시작 → event replay로 aggregate 복원**이다 | `markRehydrated`가 **두 곳에서** 불린다 — `/self-check/owner` 엔드포인트(`Program.java:95`)**와 모든 gameplay 메시지**(`GameplayMsgHandler.java:22`). 게이트는 `>= 2`다(`GameQuestStore.java:173-176`). ⇒ **클라이언트의 rehydrate 호출을 통째로 지워도 게이트가 통과한다.** 아무것도 닫히지 않고 아무것도 replay되지 않는다 |
| **SMP-JV-13** (**버그**) | [gamequest §9](../../common/sample/event/gamequest.ko.md): owner는 **최초 활성 시 event stream을 replay**한다 | `QuestStore`가 Redis에 **쓰기만 한다**(`appendQuestEvents`·`writeProjection`). **읽는 코드가 트리 전체에 없다.** replay는 JVM 안의 `List`를 읽는다. ⇒ mission 노드를 재시작하면 **모든 상태가 0에서 시작하고**, Redis에 그대로 있는 stream을 **아무도 읽지 않는다.** SoR이 장식이다 |
| **SMP-JV-14** (**버그**) | [gamequest §7](../../common/sample/event/gamequest.ko.md): notify는 **session binding을 가진 노드로 route**하고, **binding이 없으면 생략**한다 | `GameQuestSession.java:183-190` — notify를 **요청을 보낸 그 소켓**(`context.client()`)으로 민다. 대상이 누구든 상관없이. ⇒ **실제로 터진다**: 시나리오가 `player-bob`의 아이템 수집을 **alice의 세션으로** 보내고(bob은 아직 접속 전), owner가 낸 `QuestProgressNotify{playerId:"bob"}`이 **alice의 WebSocket으로 나간다.** 계약상 그 push는 **버려져야** 한다 |
| **SMP-JV-15** (**버그**) | [gamequest §9](../../common/sample/event/gamequest.ko.md): 유실되면 `GameplayStateStore`의 **누적 fact로 재계산**한다 | `SyncQuestProgressHandler.java:22` — `store.sync(playerId, 4)`. **상수 4다.** `GameplayStateStore`를 읽지 않는다. ⇒ 미발행 kill을 2개 주입하든 1개 주입하든 **보정 결과는 항상 4**이고, 단언 `currentCount >= 4`는 그래도 통과한다 |
| **SMP-JV-16** (**버그**) | [gamequest §14](../../common/sample/event/gamequest.ko.md): 같은 `IdempotencyKey` 재전송 → **진행 중복 증가 없음** | **두 가지 이유로 실패할 수 없다.** ① EventId가 dedupe 조회가 아니라 **결정적 문자열 결합**이다(`playerId + "-" + key`) — dedupe 맵을 지워도 같은 문자열이 나온다. ② 도메인이 `Math.min(required, prev + delta)`로 **clamp**하므로 3/3에서 4번째 kill은 그대로 3이고 새 event가 안 붙는다. ⇒ **dedupe 블록을 통째로 주석 처리해도 게이트가 초록이다** |
| **SMP-JV-17** (**버그**) | [gamequest §14](../../common/sample/event/gamequest.ko.md): reward idempotency | 가드가 **도메인 status 검사**다. 이미 `RewardGranted`면 같은 source event를 재적용해도 두 번째 event가 안 나온다 — **dedupe와 무관하게.** ⇒ SMP-JV-16과 같은 이유로 **실패할 수 없다** |
| **SMP-JV-18** (**버그**) | [gamequest §14](../../common/sample/event/gamequest.ko.md): reconnect = **연결 끊고 binding 해제 → 다른 노드로 재접속 → 조회로 복원** | 클라이언트가 **재접속을 하지 않는다.** alice는 api-a에만 붙고 끝이고, bob은 api-b에만 붙는다(재접속이 아니다). 유일한 단언은 "unbind가 일어났다"뿐. ⇒ **샘플의 대표 주장(같은 PlayerId가 다른 Session Server에서 같은 owner에 도달한다)을 검증하는 코드가 없다** |
| **SMP-JV-19** (**버그**) | [gamequest §9](../../common/sample/event/gamequest.ko.md): **상태 = 이벤트의 fold**. `QuestProgressed`는 `Delta`를 갖는다 | `StoredQuestEvent`에 **`Payload`도 `Delta`도 없고** 절대값 스냅샷(`currentCount`·`status`)을 싣는다. 그래서 "replay"가 **마지막 행 읽기**로 퇴화한다(Kotlin은 아예 `stream.last()`). ⇒ **최신 행 하나만 남기고 stream을 다 지워도 rebuild 게이트가 통과한다.** event sourcing이 아니다 |
| **SMP-JV-20** (**버그**) | [gamequest §8](../../common/sample/event/gamequest.ko.md): scale-out은 두 player가 **다른 owner에서 동시 처리**된다 | 모든 호출이 `.join()`으로 **즉시 블로킹**된다. alice의 전 구간이 끝난 뒤에야 bob의 세션이 열린다. **겹치는 구간이 없고**, 두 player가 다른 owner에 앉았는지 **단언하지 않는다** |
| **SMP-JV-21** (**버그**) | [샘플 규약](../../common/sample/README.ko.md): `Msg`는 **응답 없는 단방향**이다. request/reply는 `Req`/`Res`여야 한다. entry-spot → owner spot 내부 메시지도 **예외가 아니다** | `GameplayMsgHandler`가 **`ZLinkRequestHandler<GameplayMsg, QuestProcessingRes>`**다 — 계약에 없는 응답 타입을 발명해 projection과 notify 목록을 **호출자에게 되돌려준다.** 그게 SMP-JV-14(push 오라우팅)의 원인이다. C++은 명시적으로 one-way send라고 주석까지 달아 뒀다 |
| **SMP-JV-22** (결함) | [샘플 규약](../../common/sample/README.ko.md): **다른 언어 구현을 복사 기준으로 삼지 않는다** | `sample-porting-inventory.ko.md:3` — **"기준: dotnet samples/GameQuest"**. 그리고 `:19` — **"남은 gap 또는 partial 항목이 없다"**. 위 11개 버그가 전부 그 "완료" 행 아래에 있다 |

**ZoneWorld는 Java/Kotlin 구현이 아직 없다** — 계획된 순서(`dotnet → java → kotlin → node → cpp`)상 정상이며 갭이 아니다.

## 라운드 5 — TicTacToe · SupportChat 심층

**Java/Kotlin은 SupportChat에서 C++보다 훨씬 건강하다** — 날조된 게이트도, seq-vs-wallclock idle
timer도, 고객의 자기 상담원 등록도 없다. 그런데 **TicTacToe에서 새 버그가 쏟아졌다.**

### 진짜 버그

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-JV-23** (**버그**) | [tictactoe §6](../../common/sample/tictactoe/README.ko.md): room owner는 **deterministic round-robin**으로 고른다 — 첫 room은 `play-a`, 다음은 `play-b` | `CreateGameHttpHandler.java:43-45` — `return Math.min(0, playNodeCount - 1);` **항상 `0`이다.** `floorMod(counter, n)`을 잘못 쓴 것이고 카운터 자체가 없다. Kotlin도 `0.coerceAtMost(n-1)`로 같다. ⇒ **`play-b`가 방 owner가 되는 일이 한 번도 없다.** 이 샘플의 존재 이유(API 2개 × Play 2개 행렬)의 **절반이 검증되지 않는다** |
| **SMP-JV-24** (**버그**) | [tictactoe §16](../../common/sample/tictactoe/README.ko.md): 조건을 만족하지 못하면 **join을 거부하거나 오류 response를 반환해야 한다** | `PlayActorJoinGameHandler.java:26-30` — `joined.reply()`를 **분기 없이** 부르고, `actor.joinGame(roomId)`를 **그 앞에서 커밋**한다. ⇒ 거절된 join이면 actor의 게임 소속은 **이미 커밋됐고**, `Rejected(null).reply()`가 **NPE**가 되거나 payload가 있으면 클라이언트가 **일어나지도 않은 join에 성공 응답**을 받는다. Entry Spot에 앉은 채로 `PlaceMarkReq`가 handler 없는 곳으로 dispatch된다. **[갭 인덱스 §15.5]가 예측한 바로 그 실수다.** SupportChat handler 2곳은 `instanceof Accepted`로 제대로 분기한다 |
| **SMP-JV-25** (**절대 규칙 위반**) | [샘플 규약](../../common/sample/README.ko.md) | SupportChat Java가 `connectRouter(...)`를 **두 곳에서** 쓴다(`support/Program.java:82-84`, `session/Program.java:64-66`). **Kotlin은 안 쓴다 — Kotlin이 맞다.** ⇒ 자동 연결이 회귀해도 Java SupportChat은 **초록으로 남는다.** 절대 규칙이 막으려던 바로 그 실패다 |
| **SMP-JV-26** (**버그**) | [tictactoe §4](../../common/sample/tictactoe/README.ko.md): payload codec은 **JSON**이다. **MessagePack이나 Protobuf로 바꾸지 않는다** | Java·Kotlin 모두 `ZLinkMessagePackCodec`을 등록한다. ⇒ **JSON 경로의 정본 예제가 JSON을 안 쓴다** |
| **SMP-JV-27** (**버그**) | [tictactoe §17](../../common/sample/tictactoe/README.ko.md): leave는 **게임 종료 후** 단계다 | `TicTacToe Game.java:258-264` — `LeaveGameReq`에 **상태·소속 가드가 `roomId` 일치 하나뿐**이다. 게다가 `onLeaveActor`는 **push 목록만** 지우고 **도메인의 `players` 슬롯은 안 지운다.** ⇒ 게임 중에 guest가 나가면 actor는 파괴되는데 match는 **여전히 두 명이라 믿는다.** 다음 수 뒤 `nextTurn`이 **존재하지 않는 actor**를 가리키고, 방은 15초 turn timeout까지 **멈춘다.** 나간 쪽은 오류도 못 받고, 남은 쪽은 **알림도 못 받는다** |
| **SMP-KT-07** (**버그**) | [tictactoe §6](../../common/sample/tictactoe/README.ko.md): `OwnerPlayEndpoint`가 **실제 room을 만든 Play endpoint와 같아야** 한다 | **Kotlin만** — `TicTacToeGameCreator.kt:15-21`이 **모든 play endpoint에 대해 round-robin**을 도는데, 그게 **이미 `CreateGameReq`를 받은 Play 서버 위에서** 돈다. ⇒ play-a에서 만든 2번째 방이 **play-b를 owner라고 광고한다.** SMP-JV-23이 owner를 play-a에 고정해 놔서 **지금은 가려져 있다** |
| **SMP-JV-28** (**버그**) | [tictactoe §6](../../common/sample/tictactoe/README.ko.md): client는 API 응답의 `PlayNodes`로 매핑을 확인하므로 **샘플 설정의 내부 naming convention을 알 필요가 없다** | `TicTacToeGameCreator.java:28-32` — rid를 **`"play-node-" + (index+1)`로 만들어 낸다.** 진짜 rid는 config에 있다. ⇒ config의 rid 이름만 바꿔도 **서버는 계속 `play-node-1`을 광고하고 milestone push는 진짜 rid를 실어** 클라이언트 단언이 **코드 수정 없이 깨진다.** 계약이 금지한 바로 그것이다 |
| **SMP-JV-29** (**버그**) | [tictactoe §17](../../common/sample/tictactoe/README.ko.md): destroy 시퀀스는 **`LeaveGameReq`가 구동**한다 | **Java만** — `TicTacToe Game.java:183-221`의 **1초 tick**이 terminal 상태면 **`LeaveGameReq` 없이** 두 actor를 leave·destroy한다. ⇒ 릴리즈 게이트가 **타이머만으로 만족될 수 있어** 문서가 요구하는 client 구동 destroy가 **증명되지 않는다.** 게다가 tick이 `PlaceMarkRes(Won)`과 client의 `LeaveGameReq` 사이에 끼면 **간헐적 dispatch 오류**가 난다 |
| **SMP-JV-30** (**절대 규칙 위반**) | [샘플 규약](../../common/sample/README.ko.md): **TicTacToe만 수동 등록을 사용한다** | Java·Kotlin TicTacToe가 **`addHandlersFromPackageOf(...)`(패키지 스캔)**를 쓴다 — **SupportChat(자동 샘플)이 쓰는 것과 바이트 단위로 같은 호출**이다. ⇒ TicTacToe의 목적인 **수동 대 자동 대비가 JVM 샘플엔 존재하지 않는다** |
| **SMP-JV-31** (미구현) | [tictactoe §10 step 12](../../common/sample/tictactoe/README.ko.md): inbound observer + `stream-inbound` marker | `grep -rn "stream-inbound"` — Java·Kotlin **둘 다 0건.** 릴리즈 게이트 12단계가 **미구현이다** |

**Java의 TicTacToe runner가 특정 Play 이름에 게이트를 건다**(`play-b` 포트, `play-node-2`,
`play-a.log`) — 계약이 *"검증 기준은 특정 Play 이름이 아니다"*라고 못 박은 것이다.
**SMP-JV-23을 고치는 순간 그 게이트가 깨진다.**

## 라운드 5 — e2e Config 5·6·7·9·10·11 심층

**"실패할 수 없는 단언"이 이 여섯 config를 관통한다.** 아래는 그중 게이트가 **구조적으로 실패
불가능**한 것만 추렸다.

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-KT-05** (**빈 시나리오**) | [config-5 RL-D1](../../common/e2e/config-5-resilience-lifecycle.ko.md): **많은 subscriber/consumer로 높은 fanout 부하**를 주고 누락·붕괴 없이 처리되는지 본다 | Kotlin `RlD1HighFanoutScenario.kt` **파일 전체**가 이것이다 — `fun ClientScenarioContext.runHighFanoutEvidenceScenario() { println("scenario RL-D1 passed") }`. runner가 그 줄을 grep한다. **검증 코드가 0줄이다.** feature-map은 "high fanout burst에서 정상 reply를 유지하는지 확인한다"고 적는다 |
| **E2E-JV-09** (**가짜 통과**) | [config-5 RL-B4(**P0**)](../../common/e2e/config-5-resilience-lifecycle.ko.md): drain 후 신규 request가 **그 노드 evidence에 더 기록되지 않고** 다른 노드가 받는다 | `collectProviders(prefix, attempts, 1)`이 **첫 성공 응답에서 반환한다.** ⇒ provider 2개에 drain이 **완전히 망가져도** 트래픽이 50/50이라 30번 중 한 번은 살아 있는 노드에 떨어져 통과한다. **drain된 노드의 `/evidence`를 한 번도 보지 않는다.** RL-A1/A2/A4/B2/B3/B5/C2가 전부 이 helper를 쓴다 |
| **E2E-JV-10** (**가짜 통과**) | [config-5 RL-B3](../../common/e2e/config-5-resilience-lifecycle.ko.md): 종료 후 provider의 peer row가 store에서 **제거된다** | `waitForTopology(1)`이 `count >= expectedRouters`다. ⇒ 제거가 망가져 row가 2개로 남아 있어도 **`2 >= 1`이라 즉시 통과한다.** Kotlin은 올바른 helper(`waitForTopologyMissing`)를 **갖고 있으면서 여기 안 쓴다** |
| **E2E-JV-11** (**가짜 통과**) | [config-6 §3](../../common/e2e/config-6-store-failure-recovery.ko.md): harness가 Redis process를 **정지했다가 재기동**한다. SF-D2는 복구 후 각 노드가 **자기 row를 다시 upsert**하는지 본다 | Java는 Redis 앞에 **파이썬 TCP 프록시**를 세우고 그 프록시를 `kill -STOP`한다. Kotlin은 `docker pause`. ⇒ **peer row도 Redis 연결도 그대로 살아 있다.** SF-D2가 잡아야 할 결함(복구 후 row를 다시 안 올리는 구현)이 **관측 불가능하고**, Redis 클라이언트 재연결도 **한 번도 실행되지 않는다** |
| **E2E-JV-12** (**가짜 통과**) | [config-6 SF-E1](../../common/e2e/config-6-store-failure-recovery.ko.md): store client가 스레드나 이벤트 루프를 **점유하지 않음을 실측으로 증명**한다 | 지연을 **앱 데코레이터의 `CompletableFuture.delayedExecutor`**로 주입한다 — **정의상 스레드를 안 잡는 타이머**다. 진짜 Redis 클라이언트는 **전혀 느려지지 않는다.** 그리고 단언은 **자기가 넣은 타이머가 돌았는지**를 잰다. ⇒ **모든 Redis 호출마다 I/O 스레드를 붙잡는 store client도 통과한다** |
| **E2E-JV-13** (**가짜 통과**) | [config-7 MON-A2·MON-A3(**P0**)](../../common/e2e/config-7-monitoring.ko.md): **노드를 추가/종료**하고 **spot subject를 바꾼다** | 두 시나리오 모두 **트리거가 없다.** `events()`가 **지금까지 기록된 모든 event 이름의 집합**을 반환하는데, svc-a/svc-b 부팅만으로 세 kind가 다 나온다. ⇒ **클라이언트가 돌기도 전에 조건이 만족된다** |
| **E2E-JV-14** (**가짜 통과**) | [config-5 RL-D2](../../common/e2e/config-5-resilience-lifecycle.ko.md): observer 예외는 **runtime error sink로 보고된다** | Java는 **observer가 던지기 한 줄 전에 자기가 marker를 쓴다.** ⇒ framework가 그 예외를 **조용히 삼켜도** 통과한다. **Kotlin은 제대로 한다**(`RuntimeErrorEvidenceHandler`로 진짜 error sink를 본다) — **Kotlin이 앞선다** |
| **E2E-JV-15** (**가짜 통과**) | [config-9 TA-B1(**P0**)](../../common/e2e/config-9-to-actor-messaging.ko.md): 실패 분류는 **framework가 낸 public error kind**여야 한다 | caller의 `/send`·`/request`가 `actors.find(id).orElseThrow(new ZLinkFrameworkException(ACTOR_ROUTE_NOT_FOUND))`다 — **e2e 앱이 그 예외를 만든다.** `sendToActor`에 **도달조차 하지 않는다.** ⇒ framework의 분류를 **통째로 지워도 4개 단언이 다 통과한다** |
| **E2E-JV-16** (**미구현**) | [config-9 Track A(**P0 4개**)](../../common/e2e/config-9-to-actor-messaging.ko.md): **bind 상태 매트릭스**가 이 config의 표제다 | **session gateway도, stream connector도, bind도 없다.** `SERVER_ROLES=(actor caller)`뿐이다. TA-A1/A3/A4가 **TA-A2와 바이트 단위로 같은 흐름**이다. ⇒ **bind 오염 커버리지가 0이다** |
| **E2E-JV-17** (**가짜 통과**) | [config-10 §5](../../common/e2e/config-10-spot-actor-transfer.ko.md): callback order는 **단순 로그 문자열 grep이 아니라** 역할 server evidence와 flow correlation id로 검증한다 | 필수 marker 6종(`commit_request`·`location_committed`·`source_cleanup`·`handoff_backlog`·`backlog_enqueued`·`commit_ack`) 중 **5개가 서버에 존재하지 않는다.** 그래서 ST-A1의 핵심 규칙("success reply 이전에 location row가 공개되면 실패")이 **검증되지 않는다.** 그나마 runner가 보는 것은 **flow 로그 문자열 grep** — §5가 명시적으로 배제한 방법이다 |
| **E2E-JV-18** (**가짜 통과**) | [config-10](../../common/e2e/config-10-spot-actor-transfer.ko.md) | cross-node evidence 순서를 **세 JVM에서 각자 잰 `System.nanoTime()`**으로 정렬한다. `nanoTime`은 **프로세스마다 원점이 다르다.** ⇒ 원격 transfer 순서 단언 2개(P0)가 **시계 오프셋에 따라 통과/실패한다. 순서 검사가 아니다** |
| **E2E-JV-19** (**가짜 통과**) | [config-11 OBS-A2(**P0**)](../../common/e2e/config-11-observability-ops.ko.md): **dispatch error 라인**에 `flow=`가 있어야 한다 | 그 error 라인을 **클라이언트 커넥터 자신의 stderr 트레이스**에서 뽑는다. ⇒ **서버의 에러 리포터에서 `flow=`를 통째로 지워도 통과한다.** 게다가 Verifier가 검사하는 `outcome=="error"`를 **추출기가 그 선택 조건으로 부여한다** — 이중으로 반증 불가능하다 |
| **E2E-KT-06** (**가짜 통과**) | [config-11](../../common/e2e/config-11-observability-ops.ko.md) | **Kotlin config-11이 Java의 AutomaticTurnDispatch 바이너리를 역할 서버로 통째로 돌린다.** ⇒ **Kotlin 호스트가 하나도 안 뜬다.** Kotlin 고유의 metric·drain·flow 결함은 **원리적으로 안 보인다.** feature-map은 13행 전부 PASS |

**공통:** 여섯 config 어디에도 `Client/Scenarios/`가 없거나 12줄 위임 껍데기다(본문은 532~959줄
god-context). `§2.6` 환경변수 0개 규칙이 **전면 위반**인데 **feature-map 6개 중 기록한 곳이 0개**다.
Java의 `ResilienceLifecycle` Consumer와 `RuntimeMonitoring` Trigger는 **README가 이름을 찍어 금지한
시나리오 driver 서버**다 — **Kotlin은 둘 다 고쳤다.**

**`AutomaticTurnDispatch`(Java·Kotlin)가 config-8인데 `ATD-*` 네임스페이스를 쓴다** — 계약의 ID는
`TD-A1…TD-G1`이다. 두 feature-map이 **존재하지 않는 파일**(`config-8-automatic-turn-dispatch.ko.md`)을
인용한다. Java `e2e/YieldDispatch`는 **소스가 없는 죽은 빌드 디렉토리**다.
