# Framework 언어별 구현 차이

이 문서는 정식 public contract가 아니다. 공통 스펙과 언어별 스펙을 기준으로 현재
구현에서 확인된 차이를 기록한다. 차이를 해결할 때 정식 스펙을 현재 코드에 맞춰
축소하지 않고, 구현과 contract test를 정식 스펙에 맞춘다.

검토 기준일은 2026-07-12이며 대상은 `.NET`, Java/Kotlin, Node.js와 C++ framework다.

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

## 2. 전체 상태

| 영역 | `.NET` | Java/Kotlin | Node.js | C++ |
|------|--------|-------------|---------|-----|
| request와 one-way send/publish | 충족 | Java 충족, Kotlin 별도 검증 필요 | 충족 | 일반 send는 `result_t<void>`, actor send는 `task_t<void>`를 반환 |
| handler 비동기 완료 | 충족 | Java 충족, Kotlin coroutine bridge 별도 검증 필요 | 충족 | blocking bridge 차이 |
| Spot actor lifecycle | 충족 | Java 충족, Kotlin 별도 검증 필요 | 충족 | callback 이름과 실행 방식 차이 |
| typed stream session handler | 충족 | Java 충족, Kotlin 별도 검증 필요 | 충족 | raw message handler만 제공 |
| dispatch options와 diagnostics | 충족 | Java target declaration 충족, Kotlin 별도 검증 필요 | 충족 | message-flow 진단 필드와 typed event 계약 누락 |
| public export 경계 | 계약과 일치 | Java 계약과 일치, Kotlin 별도 검증 필요 | 계약과 일치 | 설치 header에 `*_state_t`와 runtime helper 노출 |
| 오류 kind | 공통 집합 충족 | 공통 집합 충족 | 공통 집합 충족 | 공통 집합 밖 값 노출 |
| route-mesh runtime options | 충족 | Java 충족, Kotlin 별도 검증 필요 | 충족 | 없음 |
| actor membership 상태 | 충족 | Java 충족, Kotlin 별도 검증 필요 | 충족 | `is_joined()`만 노출해 현재 Spot 식별자 없음 |
| actor join 결과 | 충족 | Java 충족, Kotlin 별도 검증 필요 | 충족 | result code 기반 결과가 유효 상태를 타입으로 제한하지 않음 |
| 관측·운영(metrics/flow/drain) | 충족: contract/unit/package, Bingo sample과 Config 1~11의 181개 E2E 검증 완료 | Java public declaration과 완료된 Config 6·8 검증, 나머지 Config는 이 문서에서 미검증 | 충족: contract/unit/package, sample 6개, Config 1~11의 181개 E2E와 `.NET` 교차 검증 완료 | 새 목표 계약 전체 미검증 |

## 3. Java/Kotlin

### 3.1 handler 완료가 비동기 계약과 다름

Java request, send와 publish handler는 현재 값을 직접 반환하거나 `void`로 끝난다.
정식 계약은 handler가 비동기 완료 값을 반환하면 framework가 완료까지 같은 실행 줄의
다음 callback을 시작하지 않는 것이다. Java 표면에서는 `CompletionStage<T>`와
`CompletionStage<Void>`로 이 완료를 표현해야 한다.

현재 확인 위치:

- `channels/ZLinkRequestHandler.java`: `TReply` 직접 반환
- `channels/ZLinkSendHandler.java`: `void` 반환
- `channels/ZLinkPublishHandler.java`: `void` 반환
- `actors/ZLinkActorFactory.java`: actor instance 직접 반환

Kotlin adapter는 여러 lifecycle과 actor callback을 `runBlocking`으로 Java 동기
callback에 연결한다. `CompletionStage.await()`도 내부에서 `join()`을 사용한다. 이
구현은 callback 안에서 blocking wait를 사용하지 않는다는 공통 실행 계약과 다르다.

현재 확인 위치:

- `zlink-framework-kotlin/.../ZLinkSuspendingHandlers.kt`
- `zlink-framework-kotlin/.../ZLinkCoroutineTurnAwait.kt`

### 3.2 one-way call 완료 표면

`ZLinkSendCall`, `ZLinkSessionSendCall`, `ZLinkSessionReplyCall`과
`ZLinkBoundSessionSendCall`은 현재 `ZLinkSubmitStage`를 반환하고 blocking `await()`도
제공한다. 정식 계약에서 one-way submit은 완료 객체를 반환하지 않는다. 전송 실패는
framework error observer와 runtime 진단 경로로 보고하며 request reply처럼 호출자가
완료를 기다리는 표면으로 만들지 않는다.

### 3.3 typed session handler

현재 `ZLinkTypedSessionPacketHandler`는 raw handler를 상속하고 raw method의 기본 구현이
`UnsupportedOperationException`을 던진다. 구현 누락이 컴파일 단계가 아니라 실행 중
예외로 나타나므로 정식 typed handler 계약을 충족하지 않는다. typed handler와 raw
handler의 등록 경계를 분리해야 한다.

### 3.4 Actor context 기본 예외

현재 `ZLinkActorContext.joinSpot(...)`과 `joinEntrySpot(...)`의 default method는 항상
`ZLinkConfigurationException`을 던진다. 필수 기능이면 구현자가 method를 반드시
구현하도록 하고, 선택 기능이면 별도 capability로 분리해야 한다.

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

location store/query, compression과 connector에 선언된 나머지 public extension은
Kotlin 문서의 전체 function inventory를 기준으로 검증한다. 이 항목의 남은 작업은
문서 추가가 아니라 실제 public declaration 및 contract test 정렬이다.

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

공개 options, builder와 사용자가 구현하는 extension point만 package root에 남겼다. NestJS는
framework package의 `nest-integration` subpath를 통해 내부 등록 record를 사용하므로 application
public surface에 이 구현 타입이 나타나지 않는다. source export test와 실제 `.tgz` consumer test가
이 경계를 검증한다.

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

전체 contract/unit/integration, Node 20/22 runtime matrix, sample 6개, 공통 E2E 181개,
Node.js↔`.NET` 양방향 cross-language matrix와 실제 npm tarball consumer 검증을 통과했다.
따라서 이 계획이 추적한 Node.js public contract 구현 차이는 모두 해소됐다.

## 5. C++

### 5.1 coroutine blocking bridge

Spot lifecycle과 transfer 등록 adapter가 coroutine task의 `.result()`를 호출한다.
정식 계약은 handler executor가 task 완료 때 coroutine을 재개하고 worker 또는 receive
경계를 blocking하지 않는 것이다.

현재 확인 위치는 `contracts/spots/spot.hpp`의 lifecycle adapter와 transfer adapter다.

### 5.2 오류 kind

현재 public `error_kind_t`는 공통 오류 집합 밖의 다음 값을 추가로 노출한다.

```text
actor_stale_generation
timeout
shutdown
disconnected
closed
cancelled
```

공통 오류로 채택되지 않은 값은 public framework error kind에서 제거하거나 내부 runtime
상태로 내려야 한다.

### 5.3 callback 이름

같은 Spot contract 안에서 `on_actor_join`, `on_actor_joined`, `onCreateActor`,
`onLeaveActor`가 섞여 있다. C++ public method는 `snake_case`로 통일해야 한다. 이 변경은
기존 호출자 호환성 검토가 필요한 public contract 변경이다.

### 5.4 typed session handler와 route-mesh options

현재 packet stream session handler는 raw `zlink::message_t`를 받는다. 기본 application
handler에는 typed payload 표면이 필요하다. 또한 client-server runtime options만 있고
route-mesh runtime options가 없어 channel 역할별 구성 사용성이 다르다.

### 5.5 one-way, location watch와 message-flow control

일반 one-way call은 `result_t<void> submit()`, actor send는 `task_t<void> async()`를
노출한다. 목표 계약은 모두 `void submit()`이다. 또한 location watch의
`async_range_t<T>`와 runtime `message_flow_control_t`가 public header에 없으므로 C++
언어별 interface 계약의 정확한 member를 추가해야 한다.

현재 session actor relay도 one-way call로 끝나며 bound-session disconnect는 일반 send
call을 반환한다. 다른 언어와 같은 오류 관찰 의미를 위해 relay와 disconnect는
`task_t<void>` 완료를 반환하고, bound-session send만 one-way `submit()`으로 유지한다.

### 5.6 Actor membership와 join 결과

현재 actor context는 `is_joined()`만 제공해 join 여부는 알 수 있지만 현재 user Spot의
논리 식별자를 같은 계약으로 얻을 수 없다. 목표 계약은
`std::optional<spot_rid_t> spot_rid()`를 단일 상태 값으로 제공한다. join 결과는
`std::variant`의 승인/거절 값으로 바꾸고 승인 값만 actor ref를 갖게 한다.

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

## 10. 완료 조건

각 항목은 다음 조건을 모두 만족해야 닫을 수 있다.

1. 언어별 public declaration이 정식 interface spec과 일치한다.
2. package 또는 assembly의 실제 export 목록에서 내부 구현 타입이 제거된다.
3. contract test가 전체 타입과 시그니처를 검증한다.
4. 공통 E2E가 같은 기능과 관찰 가능한 결과를 검증한다.
5. 이 문서에서 해당 차이를 제거한다.
