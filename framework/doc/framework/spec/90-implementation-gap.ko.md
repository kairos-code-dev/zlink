# Framework 언어별 구현 차이

[스펙 목차](README.ko.md) | [이전: Graceful Drain & Handoff 수명주기 계약](server/54-graceful-drain-handoff.ko.md)

이 문서는 정식 public contract가 아니다. 공통 스펙과 언어별 스펙을 기준으로 현재
구현에서 확인된 차이를 기록한다. 차이를 해결할 때 정식 스펙을 현재 코드에 맞춰
축소하지 않고, 구현과 contract test를 정식 스펙에 맞춘다.

검토 기준일은 2026-07-14이며 대상은 `.NET`, Java/Kotlin, Node.js와 C++ framework다.

## 1. 판정 기준

다음은 구현 차이다.

- 공통 스펙이 요구하는 기능이나 관찰 가능한 결과가 특정 언어에 없다.
- 언어별 스펙의 public 타입, 메서드, 반환형 또는 오류 의미가 실제 public surface와 다르다.
- 내부 구현 타입이 package root나 public contract 영역을 통해 외부에 노출된다.
- 비동기 완료를 기다려야 하는 callback이 blocking wait로 연결된다.

다음은 구현 차이가 아니다.

- 같은 기능을 `ValueTask`, `CompletionStage`, `suspend`, `Promise`, coroutine task처럼
  언어별 비동기 관례로 표현하는 차이
- interface, decorator, function object, template처럼 등록 문법이 다른 경우
- 명시적인 취소 인자가 없는 언어. 취소는 언어별 스펙이 제공하기로 한 작업에서만
  계약이며, 모든 언어의 필수 parity 항목이 아니다.

## 2. 스펙별 확인표

**공통 스펙 문서 하나가 한 행이다.** 각 칸은 그 언어 구현이 그 스펙을 충족하는지 나타낸다.

| 기호 | 뜻 |
|---|---|
| **O** | 충족 — contract/unit test와 E2E로 검증했다 |
| **△** | **부분 충족** — 일부 항목이 미해결이다. 해당 gap 절을 링크한다 |
| **X** | **미충족** — 계약을 어긴다. 해당 gap 절을 링크한다 |
| **?** | **미검증** — 이 문서가 확인하지 않았다. 충족한다는 뜻이 아니다 |
| **—** | 구현 대상이 아니다(규약·서술 문서) |

**Kotlin은 Java 런타임을 공유한다.** 표의 Kotlin 칸은 **Kotlin 고유 표면**(`suspend`, `Flow`,
DSL)이 그 스펙을 만족하는지를 뜻한다. Kotlin contract/unit/integration test와 Kotlin E2E가
Java runtime을 Kotlin 표면으로 사용해 같은 결과를 내는지 별도로 검증했다.

### 2.1 기반 (0x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 00 | [공개 계약 관리](00-public-contract-governance.ko.md) | — | — | — | — | — |
| 01 | [개요](01-overview.ko.md) | — | — | — | — | — |
| 02 | [상호작용 모델](02-interaction-model.ko.md) | O | O | O | O | O |
| 03 | [메시지 모델](03-message-model.ko.md) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) |
| 04 | [비동기 실행 정책](04-async-execution-policy.ko.md) | **X** [§12.21](#1221-yield-terminator-부재-전-언어) | **X** [§12.21](#1221-yield-terminator-부재-전-언어) | **X** [§12.21](#1221-yield-terminator-부재-전-언어) | **X** [§12.21](#1221-yield-terminator-부재-전-언어) | **X** [§12.21](#1221-yield-terminator-부재-전-언어) |
| 05 | [framework API](05-framework-api.ko.md) | O | O | O | O | O |

### 2.2 Channel (1x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 10 | [channel topology](server/10-channel-topology.ko.md) | O | O | O | O | O |
| 12 | [HTTP client](http-client/12-http-client.ko.md) | **X** [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어) | **X** [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어) | **X** [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어) | **X** [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어) | **X** [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어) |
| 11 | [channel 메시징](server/11-channel-messaging.ko.md) | **△** [§10.8](#108-dispatch-실패의-로그-수준) | O | O | **△** startup validation [§4.13](#413-startup-validation-누락-해소) | O |

### 2.3 SPOT · Actor (2x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 20 | [SPOT 메시징](server/20-spot-messaging.ko.md) | O | O | O | **△** startup validation [§4.13](#413-startup-validation-누락-해소) | O |
| 21 | [SpotNode](server/21-spot-node.ko.md) | O | O | O | O | O |
| 22 | [Actor 모델](server/22-actor-model.ko.md) | O | **X** [§12.2](#122-actor-join-admission이-선택-사항-java-c) | **X** [§12.2](#122-actor-join-admission이-선택-사항-java-c) | O | **X** [§12.2](#122-actor-join-admission이-선택-사항-java-c) |
| 23 | [Spot Actor Join/Transfer](server/23-spot-actor.ko.md) | O | **X** [§12.2](#122-actor-join-admission이-선택-사항-java-c) | **X** [§12.2](#122-actor-join-admission이-선택-사항-java-c) | O | **X** [§12.2](#122-actor-join-admission이-선택-사항-java-c) |
| 24 | [Spot 주소 메시징](server/24-spot-address-messaging.ko.md) | O | **X** [§12.9](#129-spot-전송-표면에-channel-이름을-함께-받는다-java) | **X** [§12.9](#129-spot-전송-표면에-channel-이름을-함께-받는다-java) | **X** [§12.5](#125-spot-메시징-표면-누락-node) | O |
| 25 | [Stage Wrapper](server/25-stage-wrapper-on-spot.ko.md) | O | O | O | O | O |

### 2.4 STREAM (3x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 30 | [STREAM 서버 세션](server/30-stream-session.ko.md) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) |
| 31 | [Session Actor Dispatch](server/31-session-actor-dispatch.ko.md) | O | O | O | **X** [§12.6](#126-session-handler-registry-키-node) | O |
| 32 | [Stream Connector](stream-connector/32-stream-connector.ko.md) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.1](#121-stream-connector-수신-큐-overflow-java) [§12.3](#123-근거-없는-공개-표면과-connect-상태-처리-java-kotlin) [§12.4](#124-connector-호출별-packet-name-override-java) [§12.10](#1210-connector-transport-enum-부재-java) [§12.12](#1212-connector-dispatch-mode-이름-java) [§12.13](#1213-connector-inbound-observer-option-부재-java) [§12.15](#1215-예외-정규화-부재-java) [§12.16](#1216-metadata-총-크기-한도-미검사-java) [§12.17](#1217-correlated-error-처리-java) [§12.19](#1219-typed-표면-경계-java-kotlin) [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** Java 표면 상속 + [§12.14](#1214-kotlin-option-helper가-수신-한도를-되돌린다-kotlin) [§12.19](#1219-typed-표면-경계-java-kotlin) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **X** [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) |

### 2.5 Location (4x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 40 | [location runtime](server/40-location-runtime.ko.md) | O | O | O | **X** [§12.11](#1211-location-event-kind-이름-node) | O |
| 41 | [Redis location store](server/41-location-store-redis.ko.md) | O | O | O | O | O |

### 2.6 관측 · 운영 (5x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 50 | [런타임 모니터링](server/50-runtime-monitoring.ko.md) | O | **△** [§12.8](#128-monitoring-표면-java) | **△** [§12.8](#128-monitoring-표면-java) | **X** [§12.11](#1211-location-event-kind-이름-node) | O |
| 51 | [런타임 메트릭](server/51-runtime-metrics.ko.md) | **△** [§12.7](#127-metric-drop-reason-라벨-도달-불가-net) | O | O | O | O |
| 52 | [메시지 흐름 추적](server/52-message-flow-tracing.ko.md) | O | O | O | O | O |
| 53 | [흐름 상관관계](server/53-flow-correlation.ko.md) | O | **X** [§12.18](#1218-flow_id-미전파-java) | **X** [§12.18](#1218-flow_id-미전파-java) | O | O |
| 54 | [Graceful Drain](server/54-graceful-drain-handoff.ko.md) | O | O | O | O | O |

### 2.7 열려 있는 gap 요약

**모든 X·△를 여기 모은다.** 이 목록이 비면 구현이 정본을 전부 따른 것이다.

| gap | 언어 | 내용 |
|---|---|---|
| [§4.10](#410-stream-connector-browser-only-package와-검증) | **TypeScript** | **해소.** browser-only package, 명시적 flow, 실제 Chromium과 package consumer gate가 모두 통과했다. |
| [§4.13](#413-startup-validation-누락-해소) | **Node** | **해소.** channel과 SPOT의 잘못된 구성을 socket 생성 전 startup validation에서 거부한다. |
| [§10.8](#108-dispatch-실패의-로그-수준) | **`.NET`** | dispatch 파이프라인이 `LogLevel.Error`를 넘기고도 기록을 억제해, **application 예외가 `Information`으로 평준화된다** |
| [§10.9](#109-handler-filter의-적용-범위) | (계약 범위) | filter는 **channel dispatch 경로에만** 적용한다. SPOT·STREAM·route-mesh는 우회한다. **결함이 아니라 현재 계약이다** |
| [§12.1](#121-stream-connector-수신-큐-overflow-java) | **Java** | **독립 unread-history가 없다.** overflow가 가장 오래된 메시지를 버리고(기준선은 새 메시지), 기본 상한이 무제한이며, drop 오류가 없고, handler 없는 메시지가 폐기되며, `waitFor`가 기존 메시지를 못 받고, `receivedCount` 의미가 다르며, `AUTO`에서 한도가 적용되지 않는다 |
| [§12.2](#122-actor-join-admission이-선택-사항-java-c) | **Java, C++** | `onActorJoin` admission이 **선택 사항**이라, 구현을 빠뜨리면 컴파일은 통과하고 **모든 join이 조용히 거절**된다 |
| [§12.3](#123-근거-없는-공개-표면과-connect-상태-처리-java-kotlin) | **Java, Kotlin** | connector `disconnect()`/`reconnect()`(Kotlin wrapper도 동일)와 `ZLinkActorPlacement`는 **근거 없는 표면**이다. 또 `connect()`가 진행 중인 연결 시도를 기다리지 않는다 |
| [§12.4](#124-connector-호출별-packet-name-override-java) | **Java** | 호출별 `packetName(...)` override가 없다([32 §5](stream-connector/32-stream-connector.ko.md)) |
| [§12.5](#125-spot-메시징-표면-누락-node) | **Node** | route client에 `sendToSpot`/`requestToSpot`가 없고, spot 전송의 **stale 갱신·1회 재전송이 미구현**이다 |
| [§12.6](#126-session-handler-registry-키-node) | **Node** | session handler registry가 **handler 클래스 이름**을 packet 키로 써서 wire packet name과 맞지 않는다 |
| [§12.7](#127-metric-drop-reason-라벨-도달-불가-net) | **`.NET`** | `channel.messages.dropped`의 `decode_error`·`backpressure`·`stale_route` 라벨을 발생시키는 호출부가 없다 |
| [§12.8](#128-monitoring-표면-java) | **Java** | runtime event 모델이 sealed 계층이 아니라 flat record + kind enum이고, `ZLinkMonitoringOptions`에 location 계열 source 등록 4개가 없으며, event handler가 `void`를 반환한다 |
| [§12.9](#129-spot-전송-표면에-channel-이름을-함께-받는다-java) | **Java** | `sendToSpot`/`requestToSpot`이 spot handle과 **channel 이름을 함께** 받는다. handle이 전송 mesh를 소유해야 한다 |
| [§12.10](#1210-connector-transport-enum-부재-java) | **Java** | connector가 지원 transport를 나타내는 공개 enum을 노출하지 않는다 |
| [§12.11](#1211-location-event-kind-이름-node) | **Node** | location runtime event kind가 `StoreUnavailable`이다. 닫힌 집합의 이름은 `StoreFailure`다 |
| [§12.12](#1212-connector-dispatch-mode-이름-java) | **Java** | dispatch mode enum이 `AUTO`/`MANUAL`이다(계약은 `Manual`/`Immediate`). 또 `MANUAL`에서도 state·disconnected callback이 queue를 우회하고, message callback의 완료를 기다리지 않는다 |
| [§12.13](#1213-connector-inbound-observer-option-부재-java) | **Java** | connector options에 inbound observer 큐·preview 한도 2개가 없다 |
| [§12.14](#1214-kotlin-option-helper가-수신-한도를-되돌린다-kotlin) | **Kotlin** | compression option helper가 options를 복사하며 `maxReceivedMessages`를 무제한으로 되돌린다 |
| [§12.15](#1215-예외-정규화-부재-java) | **Java** | 비동기 실패를 오류 코드를 담은 공통 예외로 정규화하지 않는다 |
| [§12.16](#1216-metadata-총-크기-한도-미검사-java) | **Java** | metadata 블록의 총합 1024바이트 한도를 검사하지 않는다 |
| [§12.17](#1217-correlated-error-처리-java) | **Java** | request sequence가 붙은 `Error`를 그 request로 매핑하면서도 **stream-level error callback으로 먼저 이중 발행**하고, error payload JSON을 파싱하지 않는다 |
| [§12.18](#1218-flow_id-미전파-java) | **Java** | inbound callback에서 시작한 send/request가 inbound의 `flow_id`를 잇지 않는다 |
| [§12.19](#1219-typed-표면-경계-java-kotlin) | **Java, Kotlin** | `send(Object)`가 raw payload를 받고, Kotlin에 목표 계약에 없는 `await<T>()` overload가 있다 |
| [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **전 언어** | `Response`·`Error` header에 packet name을 싣는다. 계약은 그 필드를 **두지 않는 것**이다 |
| [§12.21](#1221-yield-terminator-부재-전-언어) | **전 언어** | `yield` terminator가 없고 `async`가 **자동으로 turn을 반납**한다. 계약은 `async`가 turn을 유지하고 `yield`만 반납하는 것이다 |
| [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어) | **전 언어** | HTTP client에 `yield`·`submit`이 없고 DI 서버 표면도 없다. `SubmitAsync` 이름과 blocking `Fetch` 표면은 framework 계약 위반이다 |
| [§12.23](#1223-worker-축-분리와-yield-부재-전-언어) | **전 언어** | worker가 CPU/IO로 나뉘어 있지 않고, 비동기 델리게이트 오버로드와 `yield` terminator가 없다 |
| [§13](#13-샘플-연결등록-축-준수-현황) | **`.NET`, Java, Kotlin** | TicTacToe가 **수동 등록** 대신 assembly·package 스캔을 쓴다. 규약상 TicTacToe만 수동 연결 + 수동 등록이다(Node가 참조 구현) |

**connector wire 계약(§10.1~§10.7b)은 3개 구현 모두 해소했다**(§10).

## 3. Java/Kotlin

### 3.1 handler 비동기 완료

Java request, send, publish, Spot, actor와 session handler는 `CompletionStage<T>` 또는
`CompletionStage<Void>`를 반환한다.

> **turn 의미는 갭이다.** 현재 구현의 automatic turn은 handler가 stage를 **반환할 때까지**만 다음
> handler의 시작을 막고, 반환된 incomplete stage의 **완료는 기다리지 않는다.** 정본 계약은
> `async`가 **완료까지 turn을 유지**하는 것이다([04 §1.1](04-async-execution-policy.ko.md)).
> 아래 근거는 **폐기된 계약 기준의 기록**이며, 현재 갭은
> [§12.21](#1221-yield-terminator-부재-전-언어)이 소유한다.

확인 근거(구 계약 기준):

- `JavaTargetContractGapTest.handlersFactoriesAndLifecycleExposeCompletionStages`
- Config 8 `AutomaticTurnDispatch` 전체 selector — 이 config는 [config-8 실행 turn과
  terminator](../common/e2e/config-8-execution-turn.ko.md)(`TD-*`)로 대체됐다

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
[config-8 실행 turn과 terminator](../common/e2e/config-8-execution-turn.ko.md)(`TD-*`)로 대체됐다.
Config 11 전체 실행도 각 selector를
새 Redis와 새 토폴로지에서 실행하여 OBS-A1~C5가 모두 통과했다.

## 4. Node.js

### 4.1 dispatch options

언어별 스펙은 dispatch 최적화 전략을 runtime 내부에 두고 message kind별 unhandled
policy, diagnostics와 message-flow observer만 정의한다. 현재 `ZLinkDispatchOptions`는
단일 `mode`, 단일 `unhandled.action`과 제한된 diagnostics를 제공한다.

2026-07-13 구현에서 다음 항목을 정식 계약에 맞췄다.

```text
public dispatch mode 제거 완료
request/send/publish별 unhandled policy
ReplyError
LogAndDrop
Drop
includeNativeDiagnostics
localRid
peerRid
socketRole
```

현재 계약과 구현 위치는
`packages/framework/src/contracts/Dispatch/ZLinkDispatchOptions.ts`다. Config 8
`AutomaticTurnDispatch`의 전체 Node.js runner도 통과했다 — **구 계약 기준 기록**이며, 그 config는
[config-8 실행 turn과 terminator](../common/e2e/config-8-execution-turn.ko.md)(`TD-*`)로 대체됐다.

### 4.2 public export 경계

2026-07-13 구현에서 package root와 공개 `contracts/Configuration` export가 framework
내부 등록 record, normalize/validate helper와 default builder를 더 이상 내보내지 않도록
정리했다. 다음 종류의 이름은 package root에서 제거했다.

```text
createFrameworkRegistration
createFrameworkOptions
RouteChannelInternalState
MutableCodecRegistryOptions
DefaultDispatchOptionsBuilder
내부 registration record
내부 normalize/validate helper
```

공개 options, builder와 사용자가 구현하는 extension point만 package root에 남겼다. NestJS adapter는
framework package 내부의 integration bridge를 빌드 시점에 사용하지만, 이 bridge는 package export에
등록된 public subpath가 아니다. 따라서 application public surface에는 내부 등록 record와 구현 타입이
나타나지 않는다. source export test와 실제 `.tgz` consumer test가 이 경계를 검증한다.

### 4.3 typed session handler

typed payload handler와 serializer registry 연결을 구현했다. application handler에서 raw
`ZLinkMessage`를 받는 escape hatch는 제거했으며, bound session도 packet 타입으로 routing한다.

### 4.4 one-way actor와 bound session

actor와 bound session을 포함한 one-way submit을 `void submit()`으로 통일했다. 취소 신호는
actor 이동이나 session bind처럼 완료를 기다리는 장기 작업에만 남겼다.

### 4.5 interface catalog와 export 목록

언어별 interface catalog는 application public 타입의 목표 시그니처를 모두 고정한다.
location interface의 `I` prefix를 제거했다. package root의 내부 registration 타입도 제거했고,
companion NestJS package의 참조는 application export와 분리된 integration subpath로 옮겼다.

### 4.6 Actor membership와 join 결과

`isJoined`와 중복 join call을 제거하고 `spotRid`를 membership 상태 기준으로 고정했다. join
결과는 `status` discriminated union이며 승인 variant만 필수 actor ref를 가진다.

### 4.7 관측과 종료

OpenTelemetry meter `zlink.framework`, UUIDv7 flow correlation, typed graceful drain과
`session-closing` 제어 프레임을 구현했다. Node.js Config 11 `ObservabilityOps` runner는
OBS-A1~C5 evidence와 함께 통과했다. `Bingo.Ts`도 flow, metrics, drain 설정을 사용하는
sample smoke를 통과했다.

### 4.8 typed packet identity와 최종 상태

channel, route, Spot과 fanout packet identity는 `@ZLinkPacket`이 해당 class에 직접 기록한
metadata를 우선 사용하고, metadata가 없으면 생성자 이름을 사용한다. payload의
`packetName()` method와 call builder의 packet name override는 제거했다. decorator가 없는
subclass는 부모 class의 metadata를 상속하지 않는다. Stream Connector frame의 명시적 packet
name은 별도 connector 계약이므로 이 규칙의 제거 대상이 아니다.

### 4.9 stream disconnect routing id

SupportChat의 즉시 재연결 검증에서 기존 연결의 disconnect 처리와 새 actor binding이 겹치는
경합을 발견했다. Node.js framework는 같은 actor의 disconnect와 새 binding을 직렬화하고, 이전
binding token이 새 binding을 지우지 못하도록 수정했다. Stream Connector도 `close()`가 TCP 종료를
완료한 뒤 반환하도록 수정했다.

**충족.** core STREAM session은 disconnect monitor event에 peer routing id를 기록한다. Node addon은
이 값을 public `MonitorEvent.routingId`로 전달하고, framework adapter는 같은 값을 session runtime에
넘긴다. 따라서 같은 endpoint에 여러 session이 있어도 종료된 session 하나만 선택해 disconnect
callback과 binding 정리를 실행한다. routing id가 없는 이전 event를 endpoint만으로 추측하지 않는
방어 동작은 유지한다.

검증은 실제 STREAM peer를 연결·종료해 addon event의 routing id가 비어 있지 않은지 확인하고,
framework의 다중 session 회귀 검사에서 지정된 session만 종료되는지 확인했다. sample 재검토는
별도 G5 gate에서 계속 추적한다.

### 4.10 Stream Connector browser-only package와 검증

`@zlink-systems/stream-connector` package root를 플랫폼 `WebSocket` 기반 browser ESM으로 교체했다.
Node TCP/TLS, 직접 WebSocket 구현, Node flow context와 `/browser` subpath를 제거했다. public
transport는 `WebSocket`과 `WebSocketSecure`만 남으며 `tcp://`와 `tls://`는 connector를 만들 때
`ConfigurationError`로 거부한다.

브라우저 비동기 flow는 [flow correlation §4.4](server/53-flow-correlation.ko.md)의 명시적 계약을 따른다.
connector instance에는 현재 inbound flow를 저장하지 않는다. 관련 outbound는 call builder의
`flowFrom(message)`로 flow 쌍을 전달하고, 표시하지 않은 outbound는 새 application flow를 만든다.
fake WebSocket contract test에서 관련 outbound의 보존과 관련 없는 callback의 격리를 확인했다.

MessagePack과 Protobuf package root도 browser-safe payload codec만 내보내고 server serializer 등록은
`./framework` subpath로 분리했다. `stream-wire`는 같은 source의 ESM/CommonJS 산출물을 제공한다.
Bingo는 생성된 정적 encode/decode와 결정성 검사를 사용하며 runtime filesystem lookup과 `protoPath`
option을 사용하지 않는다.

실제 Chromium은 `ws`와 `wss` request/reply·push, 명시적 flow 전달과 관련 없는 callback 격리,
reconnect, drain, close reason을 검증한다. 브라우저 기본 신뢰 설정에서는 자체 서명 인증서를
거부하며, 테스트가 이를 우회하는 connector option은 없다. `close()`는 WebSocket의 실제 close
event가 올 때까지 완료되지 않는지 fake WebSocket 회귀 검사에서도 확인한다.

Node ambient type 없는 browser declaration/build, browser bundle의 Node module 부재, codec graph
분리, Bingo 생성 codec 결정성, npm tarball browser/CommonJS consumer도 통과했다. 다섯 STREAM
sample client와 네 framework E2E client를 Chromium으로 실행했고, Browser TypeScript connector에서
`.NET`과 C++ STREAM server로 보내는 cross-language smoke도 통과했다. 따라서 이 항목에 남은
public contract gap은 없다.

### 4.11 dispatch 실패 수준과 `FailCaller`

2026-07-13 재대조에서 두 가지 구현 차이를 추가로 확인하고 해소했다.

첫째, channel dispatch error reporter가 원인과 message kind에 관계없이 모든 실패를 Error로
기록했다. publish handler가 없으면 unhandled policy가 Warning을 한 번 더 기록해 중복 로그도
남았다. reporter가 handler 예외는 Error, handler 없음·decode 실패·invalid frame은 send는
Warning, publish는 Debug로 내부 결정하도록 수정했다. 공개 `ZLinkUnhandledDispatchOptions`에서
호출자가 이 계약을 바꿀 수 있던 `sendLogLevel`과 `publishLogLevel`도 제거했다.

둘째, 공통 framework API가 요구하는 `FailCaller`가 Node.js enum과 local dispatch 경로에
없었다. local Spot request와 같은 reply frame 없는 호출은 이제 caller의 Promise를 실패시키고
observer event에 `FailCaller`를 기록한다. transport reply frame을 만들 수 있는 request는
기존처럼 `ReplyError`를 사용한다.

두 항목은 contract test에서 로그 호출 횟수와 수준, local caller의 Promise 실패 및 observer
event를 함께 검증한다.

### 4.12 actor 소유권 변경 중 session relay

2026-07-13 sample 반복 검증에서 actor가 다른 Spot node로 이동하는 동안 session binding의
`ActorRef`를 갱신하는 짧은 구간에 다음 client request가 들어오면 `ActorSessionNotBound`로
실패하는 경합을 확인했다. binding 갱신은 actor별 lifecycle coordinator를 사용했지만 session
relay는 같은 직렬화 경로에 참여하지 않아, 이전 route를 제거한 뒤 새 route를 등록하기 전의
중간 상태를 관찰할 수 있었다.

session relay도 같은 actor별 lifecycle coordinator에서 실행하도록 수정했다. 이제 소유권 갱신
중 들어온 relay는 갱신 완료 뒤 새 `ActorRef`와 binding route를 사용한다. contract test는 binding
갱신을 의도적으로 중단한 동안 relay가 실패하거나 먼저 실행되지 않는지 검증한다. Bingo sample은
서로 다른 play node 사이 actor 이동 직후 client request를 반복 실행해 이 경합의 실제 경로도
검증한다.

### 4.13 startup validation 누락 (해소)

2026-07-13에 [channel 메시징 §4](server/11-channel-messaging.ko.md)와
[SPOT 메시징 §8](server/20-spot-messaging.ko.md)의 각 행을 Node.js registration validator에 직접
대입해 다음 누락을 확인했고, 같은 날 구현과 회귀 검사를 추가해 모두 해소했다.

- server에 request/send handler가 하나도 없어도 startup이 성공한다.
- subscriber에 publish handler가 하나도 없어도 startup이 성공한다.
- router와 pub/sub 역할을 모두 사용하지 않는 SpotNode가 허용된다.
- actor factory를 등록한 SpotNode에 router 역할이 없어도 허용된다.
- router 또는 pub/sub 역할을 사용하면서 bind endpoint를 지정하지 않아도 허용된다.
- location store의 자동 연결과 같은 SPOT 수신 역할의 수동 peer endpoint를 함께 지정하면
  역할별 연결 정책이 필요하다.

해소한 항목은 설정 오류를 첫 message 호출이나 연결 timeout까지 늦추므로 application 개발자가
runtime 내부 연결 조건과 구동 순서를 알아야 하는 문제로 이어진다. Node.js는 registration과
NestJS handler discovery가 끝난 뒤, socket을 만들기 전에 위 구성을
`ZLinkConfigurationException`으로 거부한다. 회귀 검사는 잘못된 구성이 startup 전에 실패하는지
검증한다.

마지막 항목은 공통 channel topology §5.2의 역할별 manual 연결 규칙을 runtime에 적용해 해소했다.
router에 manual peer가 있으면 router auto reconcile만 수행하지 않고, pub/sub에 manual endpoint가
있으면 pub/sub auto reconcile만 수행하지 않는다. location store와 actor 위치 조회는 그대로
유지한다. 따라서 TicTacToe는 sample 전용 wrapper 없이 수동 SPOT peer와 원격 actor 위치 조회를
함께 사용할 수 있다.

## 5. C++

C++ public header와 package는 이 문서가 추적하던 계약 차이를 해소했다. 아래는 각 항목의
해소 결과이며, 상세 근거는 C++ 계약 ledger와 구현 로그에 있다.

| 항목 | 해소 결과 |
|------|-----------|
| coroutine blocking bridge | 공개 계약층의 `.result()` bridge 제거(lifecycle/transfer adapter는 coroutine). runtime 내부의 동기 소비 경로는 실행 줄 소유자가 관리한다 |
| 오류 kind | 공통 집합 밖 여섯 enumerator를 public enum에서 제거하고 `detail::boundary_error_t` 내부 상태로 강등. 경계 의미는 `framework_exception_t::code()`(`std::error_code`) 파셋으로 노출 |
| callback 이름 | `on_create_actor`/`on_actor_join(ed)`/`on_leave_actor`/`on_disconnect_actor`/`destroy_actor` snake_case 통일(camelCase 탐지 경로 삭제) |
| typed session handler와 route-mesh options | `typed_session_packet_handler_for` concept과 serializer 경유 typed invoker 추가, route-mesh runtime options 정렬 |
| one-way, location watch와 message-flow control | 일반 one-way와 actor send 모두 `void submit()`, relay/disconnect는 `task_t<void>`. location watch와 message-flow 계약 표면 반영 |
| actor membership와 join 결과 | `is_joined()` 제거 후 `std::optional<spot_rid_t> spot_rid()` 단일 상태, join 결과는 승인/거절 `std::variant` |
| 관측·운영(metrics/flow/drain) | flow correlation, 계기 카탈로그, graceful drain(핸드오프·liveness·session-closing)을 구현하고 Config 1~11 E2E와 sample로 검증 |

dispatch 실패의 로그 수준([channel 메시징 §3.1](server/11-channel-messaging.ko.md))도 2026-07-13에
대조하고 정렬했다. 이전 C++ reporter는 **모든 dispatch 오류를 Error로 기록**해 원인별 구분이
없었다. 지금은 application 코드가 던진 handler 예외를 one-way라도 Error로 남기고, handler 없음·
payload decode 실패·invalid frame은 send(및 actor send)를 Warning, publish를 Debug로 낮춘다.
request는 error reply로 끝나므로 Error를 유지한다(`.NET`의 `SendLogLevel`/`PublishLogLevel`
기본값과 같은 의미). 검증은 `test_cpp_framework_message_flow`의 수준 매핑 케이스다.

### 5.1 C++ 비동기 실행 정책 — 해소

**해소(2026-07-14).** 당시의 turn 계약(자동 turn dispatch)을 검증하는 Config 8
`AutomaticTurnDispatch`가 전 시나리오 통과했다(ATD-C3B·ATD-D2 포함).

> **이후 계약이 바뀌었다.** 자동 turn dispatch는 폐기됐고 세 terminator(`submit`/`async`/`yield`)가
> 정본이다([04 §1.1](04-async-execution-policy.ko.md)). 아래 서술은 당시 계약 기준의 기록이며,
> 현재 갭은 [§12.21](#1221-yield-terminator-부재-전-언어)이 소유한다. Config 8도
> [실행 turn과 terminator](../common/e2e/config-8-execution-turn.ko.md)로 재작성했다.

간헐 실패의 원인은 turn 배선이 아니라 **stream connector의 heartbeat 응답 경로**였다. connector는
server liveness ping의 pong을 `dispatch()` 경로에서만 썼는데, ATD client는 응답을 기다리는 동안
`dispatch()`를 부르지 않는다. 그래서 수신 pump가 ping을 읽어 표시만 해 두고 pong은 나가지 않았고,
응답이 heartbeat 창보다 오래 걸리는 정상 요청에서 서버가 세션을 heartbeat timeout으로 끊었다.
client에는 그것이 `End of file`로 보였다. 지금은 수신 pump가 pong을 write 큐에 싣고, 동기 request
루프도 자기 문맥에서 바로 답한다. 추적 기록은 C++ 구현 로그의 `CPP-ATD-TIMER-RESUME-001`에 있다.

STREAM 압축 wire는 다른 언어와 같은 LZ4 pickle 프레이밍으로 정렬했다(이전 raw
`[u32][block]` 프레이밍은 언어 경계를 넘지 못했다). 남은 wire 항목은 SPOT fan-out의
단일 프레임 인코딩이며, 원인(프레임워크 부착 SPOT의 multipart publish가 첫 파트만 전달)이
core 소유라 C++ 계약 ledger에 열린 항목으로 남겨 두었다.

## 6. `.NET` 구현 상태

`.NET` public declaration과 package는 이 문서에서 추적하던 계약 차이를 해소했다.
actor membership은 nullable `SpotRid`만 상태 기준으로 사용하고, join 결과는 승인/거절
sealed record로 유효한 상태만 표현한다.

다음 타입은 기존 interface catalog에서 이름이나 전체 시그니처를 찾기 어려웠다.
현재 `.NET` interface 문서의 전체 inventory, 보완 시그니처와 공통 기능 커버리지 표에
반영했다.

```text
IZLinkActorClient
IZLinkActorDirectory
IZLinkActorJoinCall
IZLinkActorLocationStore
IZLinkActorRequestCall
IZLinkActorSendCall
IZLinkChannelRuntimeOptions
IZLinkClientServerChannelOptions
IZLinkCodecExtension
IZLinkCodecRegistrar
IZLinkLocationReadiness
IZLinkOwnerLeaseStore
IZLinkPeerLocationStore
IZLinkRouteLocationStore
IZLinkRouteMeshChannelOptions
IZLinkSpotActorLifecycle
IZLinkSpotCommonContext
IZLinkSpotLocationStore
IZLinkStreamCompressionBuilder
IZLinkUnhandledDispatchOptions
IZLinkWorkerCall
IZLinkWorkerOptions
```

`IZLinkActorSendCall`은 다른 one-way call과 같은 `void Submit(CancellationToken)` 계약을
제공한다. `SpotHandle`, capability별 `IZLinkEndpointConnections`, sealed monitoring event와
typed packet identity 단일 소유도 contract/unit/E2E 및 실제 package consumer로 검증한다.

runtime metrics, flow correlation, graceful drain과 session closing도 정식 계약, package와
Bingo 공개 예제, Config 1~11의 공통 E2E 181개로 검증했다.

> **실행 terminator는 예외다.** 위 목록이 만들어질 당시에는 "request·actor join·worker의 yield
> 전용 타입을 제거하고 단일 완료 terminator가 자동으로 turn을 관리한다"가 계약이었고, 그 기준으로
> 갭이 닫힌 것으로 기록했다. **그 계약은 폐기됐다.** 현재 정본은 세 terminator
> (`submit`/`async`/`yield`)이며([04 §1.1](04-async-execution-policy.ko.md)), `.NET`은 이를
> 충족하지 않는다. 따라서 **`.NET`에 남은 구현 차이는 [§12.20](#1220-응답에-packet-name을-싣는다-전-언어),
> [§12.21](#1221-yield-terminator-부재-전-언어), [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어),
> [§12.23](#1223-worker-축-분리와-yield-부재-전-언어)이다.** 그 밖에 이 문서가 추적하는 `.NET`
> 차이는 없다.

## 7. 문서 및 계약 검증 차이

`.NET` 문서 회귀 검사는 `spec/`(기반)·`spec/server/`와 `spec/server/languages/dotnet/`의 정식 문서를
직접 읽는다. 문서, active unit test, 실제 E2E scenario 또는 script 참조를 찾지 못하면 실패하며,
현재 G0에서는 전체 15개 검사가 성공한다. 이 검사에는 모든 공통 E2E scenario ID가 active
`.NET` fixture source와 all runner 항목에 연결되는지 확인하는 inventory 검증도 포함한다.

Java, Kotlin, Node.js와 C++는 각 언어 G0에서 같은 조건을 검증한다. 이전
`framework/<lang>/spec/` 경로를 읽거나 파일을 열지 못해도 통과하는 검사가 남아 있으면 해당
언어의 구현을 시작하기 전에 정식 경로와 fail-closed 검사로 바꾼다. 이 검증은 public interface
차이가 아니라 정식 계약을 실제로 검사하는지 확인하는 gate다.

## 8. POSD public contract 변경 gap

| 변경 | `.NET` | Java/Kotlin | Node.js | C++ |
|------|--------|-------------|---------|-----|
| Spot messaging handle | `SpotRef` resolver/outbound를 `SpotHandle`로 교체 | ref resolver와 Kotlin extension을 handle 기반으로 교체 | public structural ref를 branded handle로 교체 | `spot_ref_t` 전송 인자를 opaque `spot_handle_t`로 교체 |
| 실행 줄 관리 | request/join/worker yield 타입과 worker callback submit 제거 | blocking `await`, yield와 callback submit 제거 | yield call과 worker callback 제거 | yield call과 worker callback 제거 |
| dispatch 최적화 은닉 | `ZLinkDispatchMode`와 두 mode property 제거 | 같은 mode enum/property 제거 | 현재 `mode` option 제거 | `dispatch_mode_t`와 두 mode property 제거 |
| packet identity | typed call의 `PacketName` 제거 | typed call과 annotation override 제거 | call/payload instance override 제거 | typed call의 `packet_name` 제거 |
| actor Spot 접근 | `GetSpot` overload 제거 | Java getter 제거, Kotlin은 Spot handler 인자 사용 | `getSpot` overload 제거 | 목표 contract가 이미 getter를 요구하지 않음 |
| actor membership | `IsJoined`를 제거하고 nullable `SpotRid`만 사용 | `isJoined`를 제거하고 `Optional<RoutingId>`만 사용 | `isJoined`를 제거하고 optional `spotRid`만 사용 | `is_joined()`를 nullable `spot_rid()`로 교체 |
| actor join 결과 | boolean과 nullable actor를 승인/거절 sealed record로 교체 | result code와 nullable actor를 sealed interface로 교체 | 독립 필드를 discriminated union으로 교체 | result code 구조체를 승인/거절 `variant`로 교체 |
| monitoring event 상태 | kind와 nullable payload 독립 필드를 sealed event로 교체 | location/Spot event를 sealed interface로 교체 | event kind별 discriminated union으로 교체 | event payload도 유효 상태만 표현하는 `variant`인지 검증 |
| manual connection | capability별 `IZLinkEndpointConnections` runtime handle 추가 | 동일한 6개 nominal interface 대신 `ZLinkEndpointConnections` 재사용 | 기존 단일 interface를 builder capability accessor에 연결하고 runtime handle 의미로 정렬 | 역할 builder가 동일 connection 계약을 재사용하도록 검증 |

이 표의 변경은 public contract 변경이므로 compatibility alias를 자동으로 추가하지 않는다.
alias가 같은 복잡성을 계속 노출하면 POSD 목표를 달성하지 못한다. release 정책상 전환 기간이
필요하면 deprecated adapter를 별도 compatibility package에 두고 정식 package root에서는
새 계약만 노출한다.

## 9. 관측·운영 계약 구현 차이

2026-07-11에 확정한 runtime metrics, flow correlation, graceful drain 계약은 현재 plan의 각 언어
G0에서 실제 symbol과 source 위치를 조사한다. 기존 monitoring 또는 shutdown 기능이 일부 있어도 아래
항목 전체가 contract test로 증명되기 전에는 충족으로 판정하지 않는다.

| 영역 | 모든 언어에서 확인하고 구현할 차이 | plan 연결 |
|------|--------------------------------------|-----------|
| flow correlation | UUIDv7 id 자동 생성, 네 origin, 모든 홉과 비동기 문맥 전파·정리, `0xF2` marker codec 일괄 교체 | DN-017~018과 각 언어 G0~G3 |
| runtime metrics | 고정 catalog, server/connector 계기 소유권, 닫힌 label, fanout drop capability, 비활성 최소 비용 | DN-019와 각 언어 G0~G3 |
| graceful drain | typed `Draining` field, readiness/admission 차단, lease 유지, actor handoff와 두 SPOT 정책, 공유 terminal result | DN-020~021과 각 언어 G0~G3 |
| session closing | versioned control, 닫힌 close reason, disconnect event 순서와 bounded 전송 | DN-022와 각 언어 G0~G3 |
| 사용 예제와 배포 검증 | Bingo §17 공개 사용 예제와 Config 11 OBS-A1~C5 전체 | 각 언어 G5~G6 |

Java runtime을 공유하는 Kotlin도 별도 완료 판정을 받는다. Kotlin coroutine 문맥에서 flow가 누출되지
않는지는 `KotlinFlowContextBridgeTest`, drain waiter 취소가 shared drain을 취소하지 않는지는
`KotlinCompletionStageAwaitIntegrationTest`가 검증한다. C++는
framework가 signal handler를 설치하지 않으며 애플리케이션이 소유한 종료 실행 문맥에서 drain을
호출하는 예제를 제공해야 한다.

## 10. Stream Connector wire·검증 계약 차이

[Stream Connector 공통 스펙](stream-connector/32-stream-connector.ko.md)을 정본으로 두고 3개 connector 구현을
대조해 확인한 차이는 2026-07-13에 모두 해소했다. 브라우저 실행 환경 차이는 §4.10이
따로 소유한다.

| # | 항목 | 해소 결과 | 검증 항목 |
|---|------|-----------|-----------|
| 10.1 | **Response/Error packet name 검증 — 폐기(2026-07-14)** | 이름 대조 규칙 자체를 스펙에서 걷어냈다. pending request 매칭은 `request_seq`가 정본이고 reply의 packet name은 참고 값이다 — 이름이 달라도 응답을 버리지 않는다. C++·Node·.NET 커넥터에서 대조를 제거했다 | 이름이 다른 reply도 같은 `request_seq`의 pending request를 정상 완료한다 |
| 10.2 | **Error payload 포맷** | C++도 압축 해제 뒤 UTF-8 JSON object를 읽고 `code`와 `message`가 문자열인지 검증한다 | 올바른 Error object를 읽고 문자열이나 필수 필드가 잘못된 payload를 거부한다 |
| 10.3 | **metadata 1024바이트 한도** | C++ public option을 제거하고 송수신 양쪽에 고정된 1024바이트 한도를 적용한다 | 경계값은 허용하고 한도를 넘은 송수신 metadata는 거부한다 |
| 10.4 | **예약 packet name 범위** | C++는 `$zlink.` prefix만 거부하며 `$application.event` 같은 application 이름은 허용한다 | application 이름은 허용하고 framework 예약 prefix만 거부한다 |
| 10.5 | **수신 메시지 큐 overflow** | `.NET`은 기존 미수신 메시지를 유지하고 새 메시지를 버린 뒤 `ReceivedMessageDropped`를 보고한다 | 큐가 가득 차면 기존 항목을 유지하고 새 항목의 drop을 관찰할 수 있다 |
| 10.6 | **연결 상태 `Created`** | Java에 `CREATED`를 추가하고 첫 연결 시도 전 초기 상태로 사용한다. 연결 시도에 실패한 뒤에는 `DISCONNECTED`로 전환한다 | 최초 연결 전 상태와 연결 실패 뒤 상태를 구분한다 |
| 10.7 | **dispatch error observer의 `FailCaller` 결과** | `.NET`과 Node.js에 `FailCaller` action을 추가하고 reply frame이 만들어지지 않은 local dispatch가 caller를 실패시키며 같은 action을 보고한다 | local request에 reply가 없을 때 호출 실패 action과 원인을 함께 관찰한다 |

10.1과 10.2의 wire 호환성, 10.5와 10.7의 언어별 관찰 결과 차이는 위 구현과
회귀 검사로 같은 계약에 맞췄다.

### 10.7b `FailCaller` action (C++) — 해소

**해소.** C++ `dispatch_error_action_t`는 `reply_error`, `drop`, **`fail_caller`** 세 값을 모두
제공한다. `.NET`과 Node.js는 §10.7과 §4.11에서 해소했다. 세 언어 모두
[framework API §2.4.3](05-framework-api.ko.md)의 action 집합을 충족한다.

### 10.8 dispatch 실패의 로그 수준

**미충족(`.NET`).** [channel 메시징 §3.1](server/11-channel-messaging.ko.md)은 **handler 예외를 one-way
경로에서도 Error로 기록**하고, handler 없음·decode 실패·invalid frame은 send는 Warning, publish는
Debug로 구분하도록 규정한다.

`.NET` dispatch 파이프라인은 **`LogLevel.Error`를 넘기고도 `writeLog: false`로 실제 기록을
억제한다.** 이후 message flow tracer가 기록을 맡는데, tracer는 **shared logger로 쓸 때 오류를
`Information`으로 평준화한다.**

**정확한 조건:** logging이 활성이고(기본 `ErrorsOnly` 이상) **별도 로그 파일을 지정하지 않은**
구성에서 **오류 수준 구분이 사라진다.** 로그 파일을 지정하면 그쪽으로 출력하고 shared logger에는
남기지 않는다. 기본 구성에서는 **application 코드가 던진 예외가 정보성 로그로 묻힌다.**

### 10.9 handler filter의 적용 범위

**정보(설계 결정).** [framework API §2.6](05-framework-api.ko.md)이 규정하듯 filter는 **channel
dispatch 경로에만** 적용한다. `.NET`에서 SPOT handler·STREAM session handler·route-mesh handler는
filter 파이프라인을 거치지 않고 handler invocation engine을 직접 호출한다.

**이는 구현 결함이 아니라 현재 계약의 범위다.** SPOT과 session은 각자의 실행 문맥이 소유하는 별도
dispatch이기 때문이다. **filter를 이 경로까지 넓히려면 공개 계약을 먼저 확장해야 한다.**

## 11. 완료 조건

각 항목은 다음 조건을 모두 만족해야 닫을 수 있다.

1. 언어별 public declaration이 정식 interface spec과 일치한다.
2. package 또는 assembly의 실제 export 목록에서 내부 구현 타입이 제거된다.
3. contract test가 전체 타입과 시그니처를 검증한다.
4. 공통 E2E가 같은 기능과 관찰 가능한 결과를 검증한다.
5. 이 문서에서 해당 차이를 제거한다.

## 12. 2026-07-14 기준선 대조에서 확인한 차이

`.NET` framework 구현을 기준선으로 각 언어의 public 표면과 동작을 대조해 확인한 차이다.

**두 종류를 구분한다.** 고치는 방법이 다르기 때문이다.

| 종류 | 뜻 | 고치는 법 |
|------|-----|-----------|
| **미구현** | 계약이 요구하는 표면·동작이 **없다** | 만든다 |
| **결함** | 표면은 **있는데 계약과 다르게 동작한다** | 동작을 바꾼다. 표면 이름·시그니처가 함께 틀린 경우 그것도 바꾼다 |

**결함이 더 위험하다.** 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **그대로 통과한
채 부하가 걸릴 때만 드물게 깨진다.** 아래 표가 결함으로 분류된 항목이다.

| 갭 | 종류 | 무엇이 다른가 |
|---|---|---|
| [§12.20](#1220-응답에-packet-name을-싣는다-전-언어) | **결함** | reply를 sequence 단독으로 맞춰야 하는데 packet name을 함께 싣고 비교한다 |
| [§12.21](#1221-yield-terminator-부재-전-언어) | **결함 + 미구현** | `async`가 **자동으로 turn을 반납한다**(결함). `yield` 표면이 없다(미구현) |
| [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어) | **결함 + 미구현** | terminator 이름이 계약과 다르고 blocking 표면이 public이다(결함). turn seam·DI 서버 표면이 없다(미구현) |
| [§12.23](#1223-worker-축-분리와-yield-부재-전-언어) | **미구현** | CPU/I/O worker 분리와 worker의 `yield`가 없다 |
| [§12.24](#1224-actor-join의-orchestration이-뒤집혀-있다-전-언어) | **결함** | join이 **target 줄을 잡은 채 source 줄을 기다린다.** 그 사이클을 노드 전역 세마포어로 덮어 두었다 |

나머지 §12.1~§12.19는 언어별 표면 차이이며, 각 항목이 미구현인지 결함인지를 본문에 적었다.

### 12.1~12.19 언어별 표면 차이 → 언어별 문서로 옮겼다

기준선 대조로 찾은 **언어별 표면 차이**는 각 언어의 갭 문서가 소유한다(§16).

| 언어 | 항목 |
|------|------|
| [`.NET`](gaps/dotnet.ko.md) | §12.7 |
| [Java](gaps/java.ko.md) | §12.1~12.4 · §12.8~12.10 · §12.12·12.13 · §12.15~12.19 |
| [Kotlin](gaps/kotlin.ko.md) | §12.3 · §12.14 · §12.19 |
| [Node](gaps/node.ko.md) | §12.5 · §12.6 · §12.11 |
| [C++](gaps/cpp.ko.md) | §12.2 |

**아래 §12.20~§12.24는 전 언어 공통 계약 갭이라 여기 남긴다.**

### 12.20 응답에 packet name을 싣는다 (전 언어)

**미충족(`.NET`, Java, Node, C++).** [03 message model](03-message-model.ko.md)의 "reply
상관관계"와 [11 §3](server/11-channel-messaging.ko.md)·[30 §3.1](server/30-stream-session.ko.md)·
[32 §4](stream-connector/32-stream-connector.ko.md)는 **`Response`와 `Error` header에 packet name을 두지 않는다**고
규정한다.

**왜 두지 않는가.** 응답은 handler를 고르지 않고(dispatch key 문맥은 `Request`·`Command`·`Publish`
셋뿐이다), 어느 요청의 응답인지는 request sequence가 이미 정한다. 따라서 그 필드는 **아무도 읽지
않는 잉여**다. 실제로 지금 4개 구현이 서로 다른 값을 채워 넣고 있어 진단만 어긋난다.

| 구현 | 현재 응답에 넣는 이름 |
|------|----------------------|
| `.NET` | 요청의 packet name을 echo |
| Node | 요청의 packet name을 echo |
| C++ | 요청의 이름이 기본값이고, application이 **override할 수 있는 public 표면**까지 노출 |
| Java | **reply payload 타입의 이름**(`FooReq` 요청에 `FooRes`가 나간다) |

**고쳐야 할 것:**

- 응답 인코딩에서 packet name을 뺀다. STREAM wire는 `name_len = 0`으로 보낸다.
- C++의 reply 이름 override 표면(`stream_write_call_t::packet_name(...)`)을 제거한다.
- decoder는 구형 peer가 보낸 이름 있는 응답도 받아들이되 **무시한다**(대조 조건으로 쓰지 않는다).
- 응답의 진단·로깅에는 pending request 항목이 들고 있는 **원본 request의 이름**을 쓴다.

**대조 금지는 이미 지켜지고 있다.** 2026-07-14 전수 조사에서 5개 구현(.NET·Java·Node·C++·core)
모두 pending request를 **sequence 단독**으로 매칭하며 packet name을 대조하는 코드가 없음을
확인했다. 필드를 빼면 그 성질이 구조적으로 보장된다. 회귀 테스트로 고정한다.

### 12.21 yield terminator 부재 (전 언어)

**미충족(`.NET`, Java, Kotlin, Node, C++).** [04 §1.1](04-async-execution-policy.ko.md)은 request·
actor join·worker에 **세 terminator**를 요구한다.

| terminator | 실행 줄 |
|---|---|
| `submit` | 그대로 진행(one-way) |
| **`async`**(기본) | **turn을 유지한다.** 대기 중 같은 Spot의 다른 callback은 시작하지 않는다 |
| **`yield`**(opt-in) | turn을 반납한다. 완료된 continuation은 큐에 다시 들어가 순서대로 재개된다 |

**현재 구현은 terminator가 둘뿐이고, `async`가 자동으로 turn을 반납한다**(자동 turn dispatch).
`yield` 전용 타입은 5개 구현 모두에서 제거됐다.

**HTTP client에는 terminator 계약 자체가 없다.** `SubmitAsync<T>(ct)` 같은 평범한 awaitable
하나뿐이라 framework terminator가 아니며, spot handler에서 외부 API를 부르면 **그 시간만큼 room
전체와 timer가 멈춘다.** `yield`가 가장 필요한 자리인데 표면이 없다.

**왜 되돌려야 하는가.**

- SPOT 직렬 dispatch의 가치는 처리량이 아니라 **추론 보장**이다. "handler = 하나의 turn"이 room
  로직을 lock 없이 쓸 수 있게 하는 근거다. 자동 turn dispatch는 **코드 모양은 순차로 유지한 채
  그 보장만 없앤다** — 순차처럼 보이고, 대부분 순차로 동작하고, 부하가 걸릴 때만 드물게 깨진다.
  가장 찾기 어려운 부류의 결함이다.
- 실제로 이 스펙 문서군의 02·20·22·25가 자동 turn dispatch 도입 이후에도 **"같은 spot의 두
  handler는 동시에 실행되지 않으니 lock이 필요 없다"**를 계속 적고 있었다. 스펙을 쓴 쪽조차
  불변식이 깨진 것을 알아채지 못했다.
- **deadlock 회피는 근거가 아니다.** channel reply와 routed 메시지는 spot dispatch 루프가 꺼내며,
  그 루프는 실행 줄과 별개 축이다. turn을 유지한 채 기다려도 응답은 정상 도착한다. 요청이 자기
  Spot으로 되돌아오는 사이클만 request timeout으로 끝나는데, 그것은 application 설계 오류이며
  timeout이 올바른 결과다.
- head-of-line 지연은 실재하지만 **`yield`가 이미 그 해법**이다. opt-in으로 충분한 것을 default로
  만들면서 직렬 처리의 이점을 상쇄할 이유가 없다.

**고쳐야 할 것:**

- `async` terminator가 **turn을 유지**하도록 되돌린다. 대기 중 같은 Spot의 다음 callback을 시작하지
  않는다.
- request·actor join·worker·**HTTP client 호출**에 **`yield` terminator를 다시 제공**한다. turn을
  반납하고, 완료된 continuation을 실행 줄의 큐에 재삽입해 순서대로 재개한다.
- **HTTP client를 framework terminator 축에 올린다.** spot 실행 문맥에서 부르는 HTTP 호출은
  `submit` / `async` / `yield`를 갖는다.
- actor·timer mailbox의 재진입 차단은 그대로 둔다(`yield` 양보를 가로질러서도 유효해야 한다).
- C++ `yield` 구현의 과거 결함(blocking submit이 직렬 스레드에서 동기 실행돼 형제 timer를 굶김)은
  detached offload로 이미 해결했다. 그 방식을 유지한다.

**E2E:** `config-8`을 세 terminator 계약으로 다시 썼다([config-8 실행 turn과 terminator](../common/e2e/config-8-execution-turn.ko.md)). TD-A3(async 불변식)·TD-B1(yield 인터리브)·TD-E2(user→user join)·TD-C3(I/O worker)가 이 갭의 검증 축이다.

**샘플:** 두 공통 샘플이 `yield`를 쓰도록 규정돼 있으므로 이 갭이 풀리기 전에는 그 흐름을 구현할 수
없다.

| 샘플 | 지점 | terminator |
|------|------|-----------|
| [Bingo](../common/sample/bingo/README.ko.md) §7.1 | room Spot의 actor join/leave가 Api 서버에서 player 전적을 조회·기록한다 | `yield` |

[DeliveryDispatch §6.1](../common/sample/deliverydispatch/README.ko.md)은 entry spot의 terminator
선택 **규칙**을 소유한다(이 샘플의 entry spot 대기는 전부 자기 상태 판단이라 `async`다).

### 12.22 HTTP client가 framework 계약 밖에 있다 (전 언어)

**미충족(`.NET`, Java, Kotlin, Node, C++).** [12 HTTP client](http-client/12-http-client.ko.md)는 HTTP client를
STREAM connector와 같은 **framework 동반 client**로 규정하고 terminator·turn seam·서버 등록
표면을 고정한다. 현재는 그 축이 전부 없다.

| 항목 | 계약 | 현재 |
|------|------|------|
| terminator | `submit` / `async` / `yield` / callback | `async` 계열만(+cpp에 callback 하나). **`yield`가 5개 언어 전부 없다** |
| Spot turn 인지 | `yield`가 turn을 반납한다 | **개념 자체가 없다.** HTTP client 스펙 트리에 "spot"·"turn" 언급 0건 |
| 서버 표면 | DI 주입 client(`submit`/`async`/`yield`/callback) | **없다.** 정적 팩토리뿐이고 framework DI 등록도 없다. 실제로 **서버 코드에서 쓰는 곳이 하나도 없다** |
| terminator 이름(`.NET`) | `Async(...)` | `SubmitAsync<T>` — [04 §2](04-async-execution-policy.ko.md)가 **이름을 찍어 금지**한 형태이며, `Submit`은 one-way 전용 동사다 |
| blocking 표면 | 두지 않는다 | cpp `fetch<T>()`, `.NET` `Fetch<T>()`, Java `fetch(...)`가 public이고 **문서가 사용을 권장**한다 |

그 결과 **spot handler에서 외부 API를 호출하면 실행 줄이 그대로 막힌다.** actor 입·퇴장 시 외부
데이터를 가져오는 흐름이 room 전체와 timer를 멈춘다 — 이 client가 존재해야 하는 이유가 바로
그 경로인데 표면이 없다.

**고쳐야 할 것:**

- 세 terminator(`submit`/`async`/`yield`)와 callback 완료 경로를 제공한다. `.NET`은 `SubmitAsync` →
  `Async`로 정정한다.
- **turn seam**(execution scheduler 주입점)을 공개 계약으로 둔다. framework가 DI 등록 시 spot
  turn을 아는 scheduler를 꽂는다. C++ HTTP client에 **같은 형태의 API 표면이 이미 있다**
  (`framework_resume_scheduler_t`) — 다만 framework 런타임이 아직 그것을 주입하지 않으므로 표면만
  있고 통합은 검증되지 않았다.
- **서버용 DI 표면**을 신설한다. application이 명명 등록하고 handler가 주입받는다. 정적 팩토리는
  client-side 전용으로 남긴다.
- blocking 언래핑 terminator를 public 표면에서 제거한다.
- **바이너리 의존은 framework → HTTP client 한 방향을 유지한다.**

### 12.23 worker 축 분리와 yield 부재 (전 언어)

**미충족(`.NET`, Java, Kotlin, Node, C++).** [04 §1.2](04-async-execution-policy.ko.md)는 worker를
**CPU worker**와 **I/O worker**로 나누고, 둘 다 `async`·`yield` terminator를 갖도록 규정한다.

현재는 worker가 하나뿐이고 **동기 델리게이트만 받는다.** terminator도 `async` 하나뿐이다.

그래서 외부 I/O를 worker로 감싸면 **worker 스레드 안에서 blocking으로 기다려야 한다.** in-flight
호출 하나마다 bounded pool의 스레드 하나가 잠기고, 외부 서비스가 느려지면 pool이 고갈되어
`WorkerQueueFull`이 터진다.

**고쳐야 할 것:**

- worker를 **CPU worker**(동기 델리게이트)와 **I/O worker**(비동기 델리게이트)로 나눈다. 이름이
  실행 의미를 드러내야 한다.
- I/O worker는 **스레드를 점유하지 않는다.** 실행 줄을 다루는 경계일 뿐이며 I/O는 그대로 비동기로
  흐른다.
- 두 worker 모두 `async`(turn 유지)와 `yield`(turn 반납) terminator를 갖는다.

**함께 움직여야 하는 문서 표면.** 아래 문서들은 **현재 출하된 public 표면**(`RunWorker` 하나)을
미러하며, 일부는 회귀 테스트가 코드와 대조한다. 그래서 구현 전에 먼저 고치면 안 된다 — **구현과
같은 커밋에서 함께 바꾼다.**

| 문서 | 현재 표기 |
|------|-----------|
| `languages/<lang>/02` 인터페이스 카탈로그 | `RunWorker<TResult>(Func<CancellationToken, TResult>)` |
| 언어별 guide(예: `dotnet/guide/06-spot`, `13-interface-catalog`) | `RunWorker(...)` |
| `perf/README.ko.md` | `RunWorker`/`runWorker`/`run_worker` |
| `internals/regression-test-matrix.ko.md` | `WorkerPoolTests.RunWorker_Async_*` |

E2E는 이미 정본을 따른다 — [config-2 SM-A8](../common/e2e/config-2-spot-service.ko.md)과
[config-8 TD-C3~C5](../common/e2e/config-8-execution-turn.ko.md)가 `RunCpuWorker`/`RunIoWorker`를 쓴다.

### 12.24 actor join의 orchestration이 뒤집혀 있다 (전 언어)

**결함(`.NET`, Java, Kotlin, Node, C++).** 표면은 있고 동작한다. 그런데 **join이 target 줄을 잡은
채 source 줄을 기다린다** — 방향이 거꾸로다.

admission과 commit이 **target Spot의 줄**에서 돌고, source cleanup(`OnLeaveActor`)을 **source Spot의
큐에 post**한다. 즉 **한 join이 두 실행 줄을 걸치면서, 잡은 줄과 기다리는 줄이 반대**다.

**`.NET` 기준선 근거:**

| 사실 | 위치 |
|------|------|
| local `JoinSpot` → target activation의 `JoinActorAsync` | `Runtime/Host/ZLinkFrameworkActorFacade.cs:46-76` |
| target의 `ExecuteSerializedAsync` 안에서 admission → commit | `Runtime/Spots/ZLinkSpotActivationActors.cs:47-93` |
| commit이 source의 `NotifyActorLeftAfterManagedJoinSpotAsync`를 **기다린다** | `Runtime/Actors/ZLinkActorSessionSpotMembership.cs:13-27` |
| 그 대기가 **source 큐에 작업을 post**한다 | `Runtime/Spots/ZLinkSpotActivationActors.cs:298-311`, `379-397` |
| **노드 전역** local join 세마포어 | `Runtime/Host/ZLinkFrameworkActorFacade.cs:27-38`, `63-81` |

**그 결과 두 가지 우회가 코드에 박혀 있다.**

1. **ATD가 이 구현을 떠받치고 있다.** `JoinSpot(...).Async()`가 **source turn을 자동 반납**하므로
   source 큐가 비고 commit의 post가 실행된다([§12.21](#1221-yield-terminator-부재-전-언어)).
   **ATD를 그냥 걷어내면 user Spot → user Spot join이 즉시 막힌다.** ATD는 지연 최적화가 아니라
   **join 구현의 필수 부품**이었다.
2. **노드 전역 세마포어.** 같은 spot 쌍에서 반대 방향 join 두 개가 동시에 일어나면 서로의 큐를
   기다려 영원히 멈춘다(그 spot들의 timer와 이후 모든 join까지 함께). 그래서 구현은 **local join을
   노드 전체에서 한 번에 하나만** 처리하도록 직렬화했다. **방 입장이 프로세스 전역에서
   직렬화된다** — 사이클을 없앤 게 아니라 사이클이 생길 기회를 없앤 것이며, 그 자체로 확장성
   결함이다.

**범위:** Entry Spot actor packet은 actor mailbox로 직렬화되고 turn을 잡지 않으므로
([04 §1.1](04-async-execution-policy.ko.md)의 Entry Spot actor packet 절) **입장(Entry → user Spot)
경로는 영향이 없다.** 막히는 것은 **user Spot → user Spot join**과 user Spot handler의 `leaveActor`다.

**고쳐야 할 것 — orchestration을 caller 줄에서 돌린다:**

1. caller(source Spot 줄, turn 유지)에서 target에 **admission**을 요청하고 기다린다. target은 다른
   줄이므로 안전하다.
2. **source `OnLeaveActor`를 그 turn 안에서 inline 실행한다.** 이미 source 줄 위에 있으므로 post가
   필요 없다.
3. target **commit**과 `OnJoinedActor`를 target 줄에서 실행한다.
4. 결과를 caller에게 반환한다.

[23 §3.3~§4.1](server/23-spot-actor.ko.md)이 고정한 순서(source `OnLeaveActor` → target membership
commit → target `OnJoinedActor`)를 **그대로 지킨다.** source 큐로 되돌아가는 경로가 사라지므로
사이클이 소멸하고, **노드 전역 join 세마포어도 제거할 수 있다.**

**E2E:** [config-8 TD-E2](../common/e2e/config-8-execution-turn.ko.md)(user→user join)와
TD-E3(반대 방향 동시 join)이 이 갭의 검증 축이다.

## 13. 샘플 연결·등록 축 준수 현황

[샘플 규약](../common/sample/README.ko.md)은 두 축을 고정한다.

- **TicTacToe만** 수동 endpoint 연결 + **수동 handler 등록**을 사용한다. handler 자체는 다른
  샘플과 같이 attribute·annotation·decorator로 **선언**하되, **assembly·module 스캔에 의한 자동
  등록을 쓰지 않고** 구성 코드에서 그 handler를 직접 등록한다. 이 대조를 보여 주는 것이
  TicTacToe의 목적이다.
- **나머지 정본 샘플**은 전부 location store 자동 연결 + **자동 등록**(스캔)을 사용한다.
- **C++ 샘플은 예외**다. runtime reflection scanner가 없으므로 모든 샘플이 compile-time 명시
  등록을 쓴다([05 §3.3](05-framework-api.ko.md)). C++은 이 표의 위반 대상이 아니다.

**판정 기준은 "스캔을 쓰는가"다.** handler에 annotation을 붙였는지가 아니라, 등록이 스캔으로
일어나는지 명시 호출로 일어나는지가 기준이다.

이 축은 framework 구현이 아니라 **샘플이 지켜야 하는 규약**이다. `.NET` 구현이 기준선인 다른
항목과 달리, 여기서는 규약이 정본이고 샘플 코드가 따라와야 한다.

### 13.1 등록 축 현황 (2026-07-14 실측)

| 샘플 | 규약 | `.NET` | Java/Kotlin | Node | C++ |
|------|------|:---:|:---:|:---:|:---:|
| **TicTacToe** | **수동 등록** | **X** 스캔 | **X** 스캔 | **O** (참조 구현) | O (언어 예외) |
| Bingo | 자동 등록 | O | O | O | O (언어 예외) |
| SupportChat | 자동 등록 | O | O | O | O (언어 예외) |
| DeliveryDispatch | 자동 등록 | O | O | O | O (언어 예외) |
| ShoppingMall | 자동 등록 | O | O | O | O (언어 예외) |
| GameQuest | 자동 등록 | O | O | O | O (언어 예외) |

**Node TicTacToe가 이 축의 참조 구현이다.** handler를 decorator로 선언하고, spot의 `configure()`에서
`context.handlers.addActorPacket(...)` / `addSubscribe(..., topic)`으로 직접 등록한다. 스캔 호출이
하나도 없다. 다른 언어는 이 형태로 맞춘다.

**미충족 내용:**

- **`.NET` TicTacToe** — `AddHandlersFromAssemblyOf(...)`로 assembly를 스캔한다. attribute 선언은
  그대로 두되 스캔 호출을 빼고 spot `Configure()`와 channel builder에서 handler를 직접 등록해야
  한다.
- **Java/Kotlin TicTacToe** — `addHandlersFromPackageOf(...)`로 package를 스캔한다. annotation
  선언은 그대로 두되 스캔 호출을 빼고 handler를 직접 등록해야 한다.

### 13.2 연결 축 현황

연결 축은 규약과 일치한다. TicTacToe만 수동 endpoint(`EnableClient`/`ConnectRouter`/`ConnectPeerPub`)를
쓰고, 나머지 정본 샘플은 `AddLocationStore(...)` 자동 연결을 쓴다.

**단 ZoneWorld는 예외로 어긋나 있다.** ZoneWorld는 자동 연결 샘플인데 `dotnet` 구현이 zone 노드
사이의 spot router·pub/sub·bridge를 수동 dial한다. [10 §5](server/10-channel-topology.ko.md)의 규칙상 수동
endpoint가 하나라도 있으면 그 역할은 수동으로 확정되어 자동 연결 reconcile이 돌지 않으므로, 그
수동 배선이 오히려 자동 연결을 무력화한다. 수동 dial을 걷어내야 한다
([ZoneWorld README](../common/sample/zoneworld/README.ko.md) §4).

## 14. 문서 소유권 중복 (스펙 트리 정리 후 잔여)

spec 트리를 패키지 폴더로 나눈 뒤 드러난 **같은 계약을 두 문서가 소유하는** 자리다. 계약이
어긋나서 생긴 문제가 아니라, **어긋날 수 있는 구조**가 남아 있다는 문제다.

**지금 바로 뜯어내지 않는다.** 아래 카탈로그들은 언어별 회귀 테스트가 내용을 고정하고 있어
문서만 먼저 고치면 게이트가 깨진다. **구현 갭을 닫는 커밋에서 문서와 테스트를 함께 옮긴다.**

| 중복 | 어디 | 누가 이겨야 하나 |
|------|------|------------------|
| client connector 표면이 **서버 언어 카탈로그**에도 들어 있다 | `server/languages/dotnet/02-handler-interfaces.ko.md`, `server/languages/java/02-handler-interfaces.ko.md` | **connector 언어 문서**([stream-connector/languages/](stream-connector/README.ko.md)). 서버 카탈로그에서 뺀다 |
| Kotlin **connector coroutine·`Flow` 표면**이 서버 폴더에 있다 | `server/languages/kotlin/02-handler-interfaces.ko.md` | `stream-connector/languages/kotlin/`을 새로 만들어 옮긴다 |
| **C++ connector 계약 문서가 없다** | [32 §2](stream-connector/32-stream-connector.ko.md)는 C++ 타깃을 규정하는데 `stream-connector/languages/cpp/`가 없다 | connector 언어 문서를 만든다 |
| connector **wire header 필드**를 서버 관측 문서가 함께 정의한다 | `server/52-message-flow-tracing.ko.md`, `server/53-flow-correlation.ko.md` | **[32](stream-connector/32-stream-connector.ko.md)가 wire를 소유**한다. 52/53은 추적 **의미**만 갖고 wire는 32를 참조한다 |
| `session-closing` **인코딩과 client 디코딩**을 서버 drain 문서가 함께 정의한다 | `server/54-graceful-drain-handoff.ko.md` | **32가 wire와 connector 동작을 소유**한다. 54는 **언제·왜 보내는가**만 갖는다 |
| connector **메트릭**(`zlink.stream.reconnects`)을 서버 메트릭 문서가 정의한다 | `server/51-runtime-metrics.ko.md` | connector가 emit하는 신호는 **32**로 옮긴다. 51은 서버가 emit하는 것만 갖는다 |

**판정 기준은 "누가 그 바이트를 만드는가"다.** connector가 생성·인코딩하는 것은 32가 소유하고,
서버가 관측·해석하는 의미만 5x가 갖는다. 지금은 두 문서가 같은 header layout을 각각 적고 있어,
한쪽만 고치면 조용히 갈라진다.

## 15. 구현 감사 — 스펙과 코드를 직접 대조해 발굴한 갭

§12는 **언어 간 표면 대조**로 찾은 차이다. 이 절은 다르다 — **스펙 문장 하나하나를 코드에서
찾아 읽고** 어긋난 자리를 기록했다. 그래서 **기준선인 `.NET`에서도 갭이 나왔다.** 다른 언어를
`.NET`에 맞추는 것만으로는 잡히지 않는 것들이다.

**모든 항목은 코드 인용으로 뒷받침한다.** 근거 없는 추정은 싣지 않는다.
**상세는 언어별 갭 문서(§16)가 소유한다.**

### 15.1 대조한 축과 남은 축

| 대조함 | 아직 안 함 |
|---|---|
| 02 · 03 · 05 · 20~24 · 30 · 31 · 40 · 41 · 54 | 00 · 10 · 11 · 25 · 12(HTTP client) · 32(connector) · 50~53 |

**남은 축을 대조하기 전에는 이 목록이 완전하다고 말할 수 없다.** 감사는 **새 갭이 나오지 않을
때까지** 반복한다.

### 15.2 라운드 1 결과 (2026-07-14)

| 언어 | 발굴 | 그중 결함 | 문서 |
|------|------|-----------|------|
| `.NET` (기준선) | **7** | 7 | [gaps/dotnet](gaps/dotnet.ko.md) |
| Java / Kotlin | **10** | 6 | [gaps/java](gaps/java.ko.md) |
| Node | **10** | 6 | [gaps/node](gaps/node.ko.md) |
| C++ | **11** | 6 | [gaps/cpp](gaps/cpp.ko.md) |

**기준선에서 7건이 나온 것이 이번 라운드의 가장 큰 소득이다.** `.NET`을 정본으로 삼아 다른
언어를 맞추는 방식으로는 이 7건이 **영원히 안 보인다.**

### 15.3 교차 언어 — 같은 결함이 여러 구현에 있다

| ID | 결함 | 어디 |
|----|------|------|
| **IMP-X1** | **pending actor row(`ActorRef` 비어 있음)를 resolve 성공으로 반환한다.** [40 §2.3](server/40-location-runtime.ko.md)은 miss로 취급하라고 요구한다 | Java · C++ |
| **IMP-X2** | **location event source가 없다.** [40 §9](server/40-location-runtime.ko.md)의 `location-peer/spot/actor/route`와 `StoreFailure`/`StoreRecovered` | Java · C++ · Node |
| **IMP-X3** | **startup validation이 [20 §8](server/20-spot-messaging.ko.md)·[30 §7.2](server/30-stream-session.ko.md)의 설정 오류를 통과시킨다.** "모든 설정 오류는 host 시작 전에 실패한다"가 계약 | **네 언어 모두** |
| **IMP-X4** | **location store read에 5초 취소 상한이 없다.** [54 §3.4](server/54-graceful-drain-handoff.ko.md)가 framework 내부 정책으로 고정 | `.NET` · Java — **기준선에도 없는 구멍** |

## 16. 언어별 갭 체크리스트

**언어별 갭은 아래 문서가 소유한다.** 각 문서는 체크리스트이며, **계약이 아니라 작업 목록**이다.

| 언어 | 문서 | 항목 수 |
|------|------|---------|
| `.NET` | [gaps/dotnet](gaps/dotnet.ko.md) | 15 |
| Java | [gaps/java](gaps/java.ko.md) | 33 |
| Kotlin | [gaps/kotlin](gaps/kotlin.ko.md) | 8 |
| Node.js | [gaps/node](gaps/node.ko.md) | 20 |
| C++ | [gaps/cpp](gaps/cpp.ko.md) | 20 |

각 문서는 세 묶음을 담는다 — **구현 감사에서 발굴한 것**(IMP-*), **교차 언어 결함**(IMP-X*),
**언어별 표면 차이**(§12.x). 전 언어 공통 계약 갭(§12.20~§12.24)은 이 문서가 소유하고 각 언어
체크리스트가 참조한다.
