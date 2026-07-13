# Framework 언어별 구현 차이

[스펙 목차](README.ko.md) | [이전: Graceful Drain & Handoff 수명주기 계약](54-graceful-drain-handoff.ko.md)

이 문서는 정식 public contract가 아니다. 공통 스펙과 언어별 스펙을 기준으로 현재
구현에서 확인된 차이를 기록한다. 차이를 해결할 때 정식 스펙을 현재 코드에 맞춰
축소하지 않고, 구현과 contract test를 정식 스펙에 맞춘다.

검토 기준일은 2026-07-13이며 대상은 `.NET`, Java/Kotlin, Node.js와 C++ framework다.

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
DSL)이 그 스펙을 만족하는지를 뜻하며, 대부분 별도 검증을 하지 않았다.

### 2.1 기반 (0x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 00 | [공개 계약 관리](00-public-contract-governance.ko.md) | — | — | — | — | — |
| 01 | [개요](01-overview.ko.md) | — | — | — | — | — |
| 02 | [상호작용 모델](02-interaction-model.ko.md) | O | O | ? | O | O |
| 03 | [메시지 모델](03-message-model.ko.md) | O | O | ? | O | O |
| 04 | [비동기 실행 정책](04-async-execution-policy.ko.md) | O | O | **?** coroutine bridge 미검증 | O | O |
| 05 | [framework API](05-framework-api.ko.md) | O | O | ? | O | **△** `FailCaller` 없음 [§10.7b](#107b-failcaller-action-c) |

### 2.2 Channel (1x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 10 | [channel topology](10-channel-topology.ko.md) | O | O | ? | O | O |
| 11 | [channel 메시징](11-channel-messaging.ko.md) | **△** [§10.8](#108-dispatch-실패의-로그-수준) | ? 로그 수준 미대조 | ? | ? 로그 수준 미대조 | ? 로그 수준 미대조 |

### 2.3 SPOT · Actor (2x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 20 | [SPOT 메시징](20-spot-messaging.ko.md) | O | O | ? | O | O |
| 21 | [SpotNode](21-spot-node.ko.md) | O | O | ? | O | O |
| 22 | [Actor 모델](22-actor-model.ko.md) | O | O | ? | O | O |
| 23 | [Spot Actor Join/Transfer](23-spot-actor.ko.md) | O | O | ? | O | O |
| 24 | [Spot 주소 메시징](24-spot-address-messaging.ko.md) | O | O | ? | O | O |
| 25 | [Stage Wrapper](25-stage-wrapper-on-spot.ko.md) | O | O | ? | O | O |

### 2.4 STREAM (3x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 30 | [STREAM 서버 세션](30-stream-session.ko.md) | O | O | ? | O | O |
| 31 | [Session Actor Dispatch](31-session-actor-dispatch.ko.md) | O | O | ? | O | O |
| 32 | [Stream Connector](32-stream-connector.ko.md) | O | O | ? | **△** 브라우저 flow 문맥 [§4.10](#410-stream-connector-브라우저-진입점과-비동기-flow-문맥) | O |

### 2.5 Location (4x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 40 | [location runtime](40-location-runtime.ko.md) | O | O | ? | O | O |
| 41 | [Redis location store](41-location-store-redis.ko.md) | O | O | ? | O | O |

### 2.6 관측 · 운영 (5x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 50 | [런타임 모니터링](50-runtime-monitoring.ko.md) | O | O | ? | O | O |
| 51 | [런타임 메트릭](51-runtime-metrics.ko.md) | O | **△** Config 6·8만 검증 | ? | O | O |
| 52 | [메시지 흐름 추적](52-message-flow-tracing.ko.md) | O | **△** Config 6·8만 검증 | ? | **△** 브라우저 `MFLOW-EXT-014` [§4.10](#410-stream-connector-브라우저-진입점과-비동기-flow-문맥) | O |
| 53 | [흐름 상관관계](53-flow-correlation.ko.md) | O | ? | ? | O | O |
| 54 | [Graceful Drain](54-graceful-drain-handoff.ko.md) | O | **△** Config 6·8만 검증 | ? | O | O |

### 2.7 열려 있는 gap 요약

**모든 X·△를 여기 모은다.** 이 목록이 비면 구현이 정본을 전부 따른 것이다.

| gap | 언어 | 내용 |
|---|---|---|
| [§4.10](#410-stream-connector-브라우저-진입점과-비동기-flow-문맥) | **Node** | 브라우저 진입점에는 `AsyncLocalStorage`에 해당하는 표준이 없어 **handler가 `await`하는 동안 관련 없는 callback에 inbound flow가 노출될 수 있다.** `MFLOW-EXT-014` 미충족 |
| [§10.7b](#107b-failcaller-action-c) | **C++** | dispatch error event의 `action`에 **`FailCaller`가 없다.** reply frame 없는 경로의 실패를 **관측할 수 없다** |
| [§10.8](#108-dispatch-실패의-로그-수준) | **`.NET`** | dispatch 파이프라인이 `LogLevel.Error`를 넘기고도 기록을 억제해, **application 예외가 `Information`으로 평준화된다** |
| [§10.9](#109-handler-filter의-적용-범위) | (계약 범위) | filter는 **channel dispatch 경로에만** 적용한다. SPOT·STREAM·route-mesh는 우회한다. **결함이 아니라 현재 계약이다** |
| 전 영역 | **Kotlin** | Kotlin 고유 표면(`suspend`·`Flow`·DSL)이 각 스펙을 만족하는지 **이 문서가 검증하지 않았다** |
| §51·§52·§54 | **Java** | 관측·운영 계약을 **Config 6·8에서만 검증했다.** 나머지 Config는 미검증 |

**connector wire 계약(§10.1~§10.7)은 2026-07-13에 3개 구현 모두 해소했다**(§10).

## 3. Java/Kotlin

### 3.1 handler 비동기 완료

Java request, send, publish, Spot, actor와 session handler는 `CompletionStage<T>` 또는
`CompletionStage<Void>`를 반환한다. automatic turn은 handler가 stage를 반환할 때까지 다음
handler의 시작 순서를 보장하며, 반환된 incomplete stage의 완료는 기다리지 않는다.

확인 근거:

- `JavaTargetContractGapTest.handlersFactoriesAndLifecycleExposeCompletionStages`
- Config 8 `AutomaticTurnDispatch` 전체 selector

Kotlin adapter는 여러 lifecycle과 actor callback을 `runBlocking`으로 Java 동기
callback에 연결한다. `CompletionStage.await()`도 내부에서 `join()`을 사용한다. 이
구현은 callback 안에서 blocking wait를 사용하지 않는다는 공통 실행 계약과 다르다.

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
awaitReply
send
publishToTopic
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

### 3.7 Java 검증 상태

Java target public declaration은 `JavaTargetContractGapTest` 전체 통과와 production symbol
검색으로 확인했다. Config 6 `StoreFailure`의 SF-A1~E1과 Config 8
`AutomaticTurnDispatch`의 정식 selector는 real E2E 전체 실행을 통과했다. 다른 Config와
sample은 이 갱신에서 다시 실행하지 않았으므로 Java 전체 완료 증거로 사용하지 않는다.

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
`AutomaticTurnDispatch`의 전체 Node.js runner도 통과했다.

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

### 4.10 Stream Connector 브라우저 진입점과 비동기 flow 문맥

**transport 충족, flow 문맥 미충족.** `@zlink-systems/stream-connector/browser`를 추가하고 플랫폼의 네이티브
`WebSocket`을 기본 transport로 연결했다. 브라우저 진입점은 `ws://`와 `wss://`만 허용하며,
`tcp://`와 `tls://`는 connector를 만들 때 `ConfigurationError`로 거부한다.

공용 runtime에서 `node:async_hooks`, `node:crypto`와 Node 기본 transport의 정적 import를
제거했다. Node 진입점만 해당 구현을 선택하므로 브라우저 bundle 그래프에는 `net`, `tls`,
`async_hooks`, `crypto` Node 모듈과 `Buffer`가 포함되지 않는다. 두 진입점은 같은 public export
목록과 connector option·interface type을 유지한다.

검증은 다음 범위를 통과했다.

- esbuild로 생성한 실제 browser bundle과 module graph 검사
- 플랫폼 `WebSocket` event 계약을 사용한 WSS request/reply·push 수신 smoke
- `tcp://`·`tls://` 즉시 거부 contract test
- Node Stream Connector contract test 전체
- 실제 npm tarball의 Node·browser runtime import와 TypeScript 소비자 compile

검증 환경에 headless 브라우저 실행 도구가 없어 실제 브라우저 프로세스는 실행하지 않았다.
배포 전 브라우저 호환성 확인에서는 같은 WSS 시나리오를 실제 브라우저에서도 실행한다.

남은 gap은 [flow correlation MFLOW-EXT-014](53-flow-correlation.ko.md)의 비동기 실행 문맥이다.
현재 `BrowserZlinkFlowContext`는 한 connector instance의 current flow를 handler Promise가 끝날 때까지
유지한다. 그래서 handler가 `await`로 기다리는 동안 관련 없는 timer나 UI callback이 같은 connector로
메시지를 보내면 inbound flow를 잘못 재사용할 수 있다. callback 직후 current flow를 복원하면
`await` 이후 continuation이 flow를 잃으므로 해결이 아니다.

브라우저 표준에는 `AsyncLocalStorage`에 해당하는 기능이 없고, 저장소의 기존 dependency에도 이를
대신할 수단이 없다. `zone.js` 0.16.2도 native async function의 첫 `await` 뒤 child Zone을 보존하지
못해 계약 테스트의 기반으로 사용할 수 없었다. public callback에 flow-bound sender를 추가하면
공통 스펙이 금지한 v1 capture/wrap 표면을 새로 만들게 된다. 따라서 불완전한 전역 변수나 Promise
patch를 추가하지 않고, 브라우저 async-context 환경 또는 public callback 계약을 별도 설계로
확정할 때까지 이 항목을 Node.js G2 public runtime gap으로 유지한다. G2가 열려 있으므로
후속 build·test gate인 G3와 최종 재검토 gate인 G7도 완료로 표시하지 않는다.

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

request, actor join과 worker의 yield 전용 타입은 source와 package에서 제거했으며 단일
완료 terminator가 자동으로 turn을 관리한다. `IZLinkActorSendCall`도 다른 one-way call과
같은 `void Submit(CancellationToken)` 계약을 제공한다. `SpotHandle`, capability별
`IZLinkEndpointConnections`, sealed monitoring event와 typed packet identity 단일 소유도
contract/unit/E2E 및 실제 package consumer로 검증한다.

runtime metrics, flow correlation, graceful drain과 session closing도 정식 계약, package와
Bingo 공개 예제, Config 1~11의 공통 E2E 181개로 검증했다. 따라서 이 문서에서 추적하는
`.NET` 구현 차이는 남아 있지 않다.

## 7. 문서 및 계약 검증 차이

`.NET` 문서 회귀 검사는 `common/spec/`과 `common/spec/languages/dotnet/`의 정식 문서를
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
않고 drain waiter 취소가 shared drain을 취소하지 않는다는 Kotlin 전용 test가 필요하다. C++는
framework가 signal handler를 설치하지 않으며 애플리케이션이 소유한 종료 실행 문맥에서 drain을
호출하는 예제를 제공해야 한다.

## 10. Stream Connector wire·검증 계약 차이

[Stream Connector 공통 스펙](32-stream-connector.ko.md)을 정본으로 두고 3개 connector 구현을
대조해 확인한 차이는 2026-07-13에 모두 해소했다. 브라우저 실행 환경 차이는 §4.10이
따로 소유한다.

| # | 항목 | 해소 결과 | 검증 항목 |
|---|------|-----------|-----------|
| 10.1 | **Response/Error packet name 검증** | `.NET`과 C++도 request name을 pending 상태에 보존하고 reply 이름이 다르면 `FrameDecodeFailed`로 완료한다 | 정상 reply와 이름이 다른 reply를 구분하고 pending request를 실패로 완료한다 |
| 10.2 | **Error payload 포맷** | C++도 압축 해제 뒤 UTF-8 JSON object를 읽고 `code`와 `message`가 문자열인지 검증한다 | 올바른 Error object를 읽고 문자열이나 필수 필드가 잘못된 payload를 거부한다 |
| 10.3 | **metadata 1024바이트 한도** | C++ public option을 제거하고 송수신 양쪽에 고정된 1024바이트 한도를 적용한다 | 경계값은 허용하고 한도를 넘은 송수신 metadata는 거부한다 |
| 10.4 | **예약 packet name 범위** | C++는 `$zlink.` prefix만 거부하며 `$application.event` 같은 application 이름은 허용한다 | application 이름은 허용하고 framework 예약 prefix만 거부한다 |
| 10.5 | **수신 메시지 큐 overflow** | `.NET`은 기존 미수신 메시지를 유지하고 새 메시지를 버린 뒤 `ReceivedMessageDropped`를 보고한다 | 큐가 가득 차면 기존 항목을 유지하고 새 항목의 drop을 관찰할 수 있다 |
| 10.6 | **연결 상태 `Created`** | Java에 `CREATED`를 추가하고 첫 연결 시도 전 초기 상태로 사용한다. 연결 시도에 실패한 뒤에는 `DISCONNECTED`로 전환한다 | 최초 연결 전 상태와 연결 실패 뒤 상태를 구분한다 |
| 10.7 | **dispatch error observer의 `FailCaller` 결과** | `.NET`에 `FailCaller` action을 추가하고 reply가 만들어지지 않은 local dispatch를 `ReplyPathMissing`과 함께 보고한다 | local request에 reply가 없을 때 호출 실패 action과 원인을 함께 관찰한다 |

10.1과 10.2의 wire 호환성, 10.5와 10.7의 언어별 관찰 결과 차이는 위 구현과
회귀 검사로 같은 계약에 맞췄다.

### 10.7b `FailCaller` action (C++)

**미충족(C++).** [framework API §2.4.3](05-framework-api.ko.md)은 dispatch error event의 `action`에
**`FailCaller`**(reply frame이 없는 경로에서 caller를 framework 오류로 완료)를 요구한다.

C++ `dispatch_error_action_t`에는 **`reply_error`와 `drop` 두 값뿐**이다. `reply_path_missing`
reason은 enum에 있으나 이를 표현할 action이 없어, **이 경로의 dispatch 실패를 관측할 수 없다.**
`.NET`은 §10.7에서 해소했다.

### 10.8 dispatch 실패의 로그 수준

**미충족(`.NET`).** [channel 메시징 §3.1](11-channel-messaging.ko.md)은 **handler 예외를 one-way
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
