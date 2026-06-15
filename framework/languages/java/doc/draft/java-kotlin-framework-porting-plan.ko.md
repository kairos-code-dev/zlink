<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [Java 묶음](./README.ko.md) | [다음: Draft -- ZLink Framework Java Interface Catalog](./handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

# Draft -- Java/Kotlin Framework Porting Plan

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` framework와 같은 기능을 `Java`/`Kotlin`으로
> 포팅하기 위한 구현 기준과 작업 순서를 정리한다.

## 1. 기준과 범위

포팅 기준은 현재 `.NET` framework 코드다. 이전 Java 초안을 그대로 구현하는 것이
아니라, 아래 세 묶음의 역할을 Java/Kotlin에 맞게 옮긴다.

| `.NET` 기준 | Java/Kotlin 대응 |
|-------------|------------------|
| `Zlink.Framework` | `systems.zlink.framework` core module |
| `Zlink.Framework.AspNetCore` | `systems.zlink.framework.spring` Spring Boot adapter |
| `Systems.Zlink.Stream.Connector` | `systems.zlink.stream.connector` client connector module |

Java/Kotlin 포팅은 두 표면을 함께 제공한다.

- Java 표면은 `CompletionStage`, `AutoCloseable`, Spring bean, annotation을 기준으로
  잡는다.
- Java binding도 public async 반환은 `CompletionStage`를 기준으로 둔다. 내부 완료
  제어에는 `CompletableFuture`를 쓸 수 있지만, public contract에서 blocking 대기를
  쉽게 유도하지 않는다.
- Kotlin 표면은 Java API 위에 얇은 extension을 얹어 `suspend`, `Flow`,
  DSL builder를 제공한다. Kotlin 전용 런타임을 따로 만들지 않는다.

## 2. 모듈 구조

> **정규 이름표는 [실행 계획 §0](./implementation-execution-plan.ko.md#0-정규-모듈패키지-이름표-phase-0-dod-기준)**
> 이 단일 기준이다. Gradle artifact ↔ Java package 매핑이 이 문서와 어긋나면
> 실행 계획 §0을 따른다. 아래는 역할 요약이다.

모듈은 아래처럼 나눈다. (binding group `systems.zlink`)

| 모듈 (Gradle artifact) | Java package | 역할 |
|------------------------|--------------|------|
| `zlink-framework-core` | `systems.zlink.framework` | framework public contracts, runtime, handler scanner, backend adapter port |
| `zlink-framework-spring-boot-starter` | `systems.zlink.framework.spring` | auto configuration, `SmartLifecycle`, bean discovery, event bridge |
| `zlink-stream-connector` | `systems.zlink.stream.connector` | client STREAM connector core, heartbeat, reconnect |
| `zlink-stream-connector-codecs` | `systems.zlink.stream.connector.codecs` | codec contract + auto codec selector (공유) |
| `zlink-stream-connector-json` | `systems.zlink.stream.connector.json` | JSON codec |
| `zlink-stream-connector-msgpack` | `systems.zlink.stream.connector.msgpack` | MessagePack codec |
| `zlink-stream-connector-protobuf` | `systems.zlink.stream.connector.protobuf` | Protobuf codec |
| `zlink-framework-kotlin` | `systems.zlink.framework.kotlin` | coroutine/DSL extension. core runtime을 다시 구현하지 않는다 |
| `zlink-framework-testkit` | `systems.zlink.framework.testkit` | in-process test host, fake backend, contract fixture |

codec은 `.NET` `.Codecs/.Json/.MessagePack/.Protobuf`처럼 **공유 codec-contract 모듈
(`-codecs`) 위의 별도 모듈**로 둔다. connector core(`zlink-stream-connector`)는 codec
contract만 의존하고 구체 codec은 선택적으로 얹는다. 하나의 connector 모듈에 codec을
합치지 않는다.

`zlink-framework-core` 내부 package는 개념 단위로 둔다.

| 패키지 | 포함 항목 |
|--------|-----------|
| `systems.zlink.framework.channels` | channel client, fanout, route client, handler context |
| `systems.zlink.framework.spots` | Spot, SpotNode, Spot manager, timer, route acceptance |
| `systems.zlink.framework.actors` | actor, actor factory, actor manager, bound session |
| `systems.zlink.framework.streams` | stream node, session, session context, stream error |
| `systems.zlink.framework.registry` | embedded registry, registry query client |
| `systems.zlink.framework.monitoring` | runtime event, event mapper, polling runner |
| `systems.zlink.framework.configuration` | option builders and validators |
| `systems.zlink.framework.execution` | serial execution queue (`.NET` `Runtime/Execution/` 미러 — Phase 5 산출, Phase 6/7 재사용) |
| `systems.zlink.framework.runtime` | internal runtime namespace only |
| `systems.zlink.framework.runtime.host` | framework runtime host composition |
| `systems.zlink.framework.runtime.configuration` | runtime registration model and builder adapters |
| `systems.zlink.framework.runtime.backend` | backend adapter ports between framework runtime and Java binding |
| `systems.zlink.framework.runtime.actors` | actor runtime, session actor binding, bound session support |
| `systems.zlink.framework.runtime.channels` | channel runtime, channel registrations, channel builder adapters |
| `systems.zlink.framework.runtime.spots` | spot runtime and spot node registrations |
| `systems.zlink.framework.runtime.streams` | stream runtime and stream node registrations |
| `systems.zlink.framework.runtime.registry` | embedded registry runtime and remote registry query client |
| `systems.zlink.framework.runtime.monitoring` | monitoring runtime and monitoring option model |
| `systems.zlink.framework.runtime.messaging` | message serializer and runtime message helpers |

runtime 루트에는 개별 구현 파일을 두지 않고, `.NET` `Runtime/*` 카테고리에 대응하는
하위 package가 실제 구현을 가진다.

## 3. Public Surface 대응표

| 기능 | `.NET` 이름 | Java 이름 | Kotlin 보조 표면 |
|------|-------------|-----------|------------------|
| framework 등록 | `AddZLinkFramework` | `@EnableZLinkFramework`, `ZLinkFrameworkConfigurer` | `zlinkFramework { ... }` |
| channel request/send | `IZLinkChannelClient` | `ZLinkClient` | `suspend fun ZLinkClient.request(...)` |
| fanout publish | `IZLinkFanoutClient` | `ZLinkFanoutClient` | `suspend fun publish(...)` |
| routed channel | `IZLinkRouteClient` | `ZLinkRouteClient` | `suspend fun requestTo(...)` |
| Spot manager | `IZLinkSpotManager` | `ZLinkSpotManager` | `suspend fun getOrCreate<T>()` |
| Spot outbound | `IZLinkSpotOutbound` | `ZLinkSpotOutbound` (계약), `ZLinkSpotClient`(구현) | typed extension |
| actor manager | `IZLinkActorManager` | `ZLinkActorManager` | `suspend fun getOrCreateActor(...)` |
| stream session | `IZLinkSession` | `ZLinkSession` | coroutine session adapter |
| bound session | `IZLinkBoundSession` | `ZLinkBoundSession` | `suspend fun send(...)` |
| monitoring | `IZLinkRuntimeEventHandler<T>` | `ZLinkRuntimeEventHandler<T>` | `Flow<ZLinkRuntimeEvent>` |
| stream connector | `IZlinkStreamConnector` | `ZLinkStreamConnector` | `Flow<ZLinkStreamMessage<*>>` |

## 4. 구현 순서

> **순서의 단일 권위는 [실행 계획](./implementation-execution-plan.ko.md)이다.**
> 이 §4의 단계 번호가 실행 계획의 phase 번호와 어긋나면 **실행 계획 phase 순서를
> 따른다.** 아래는 모듈별 책임을 묶어 본 개요이며, 실제 순서는 다음과 같다(실행
> 계획과 동일): **Phase 0 골격 → Phase 1 contract+adapter → Phase 1.5 binding parity
> → Phase 2 channel messaging(수직 슬라이스 1) → Phase 3 Spring Boot starter →
> Phase 4 registry/monitoring → Phase 5 Spot → Phase 6 actor → Phase 7 STREAM session
> → Phase 8 connector → Phase 9 Kotlin → Phase 10 samples → Phase 11 docs.**
>
> 즉 **host/starter(Phase 3)가 registry/spot(Phase 4/5)보다 앞서고**, 수직 슬라이스
> 1은 **channel messaging**이다. 아래 4.1~4.6의 "n단계" 표기는 모듈 묶음 설명용일
> 뿐 phase 번호가 아니다.

### 4.1 1단계: contract와 backend adapter

먼저 public contract와 backend adapter를 만든다. framework가 Java binding의
public API만 호출해야 하므로, 필요한 기능이 Java binding에 없으면 binding public
API를 추가하고 framework에서 internal 접근을 우회하지 않는다.

필수 산출물은 아래와 같다.

- `ZLinkFrameworkOptions`, channel/spot/stream builder
- `ZLinkBackendAdapterFactory`와 channel, spot, registry, monitoring, stream adapter
- `ZLinkMessageSerializer`와 기본 JSON codec
- handler scanner와 annotation metadata model
- builder validator

backend adapter는 `.NET`의 internal adapter 계층과 같은 책임을 갖지만, Java
framework public 표면에는 노출하지 않는다. 구현자는 아래 adapter 계약을 먼저 닫은
뒤 channel, registry, spot, stream runtime을 얹는다.

| Adapter | 책임 |
|---------|------|
| context adapter | Java binding context 생성, lifecycle, native object 소유권 |
| socket adapter | dealer/router/pub/sub send, request, receive pump, monitor socket 조회 |
| registry adapter | embedded registry 생성, registry query client 생성 |
| spot adapter | spot node 생성, actor gateway attach, bound session send/close |
| stream adapter | stream socket bind, session attach, frame send/reply |
| monitoring adapter | native socket/discovery monitor event 변환 |

adapter 밖에서는 native handle, internal binding type, reflection을 알 수 없어야 한다.
필요한 binding 기능이 public API에 없으면 binding에 public API를 추가한 뒤 adapter가
그 API를 호출한다.

### 4.2 2단계: channel messaging

`.NET`의 `ZLinkChannelRuntimeManager`, channel dispatch pipeline, submit queue를
Java로 옮긴다. dispatch 기준은 local `ROUTER(server)` ingress다. outbound
`DEALER(client)` 수신은 reply correlation으로만 처리한다.

구현할 표면은 아래와 같다.

- `ZLinkClient.sendToChannel(...).submit()`
- `ZLinkClient.requestToChannel(...).timeout(...).submit(...)`
- `ZLinkFanoutClient.publish(...).submit()`
- `ZLinkRouteClient.send(...).submit()`, `request(...).submit(...)`
- `@ZLinkRequest`, `@ZLinkSend`, `@ZLinkPublish` (annotation은 `Mapping` 접미사
  없이, publish는 `Event`가 아니다 — 함정표 §10 참조)
- handler interface 기반 자동 등록
- 역할별 manual `connect`, `disconnect`, `listConnections`

### 4.3 3단계: Registry와 Monitoring

Registry는 embedded registry와 query client를 분리한다. request hot path는 registry
query가 아니라 각 channel/spot runtime의 discovery view를 사용한다.

Monitoring은 socket/discovery native monitor와 registry/spot snapshot diff를 typed
event로 올린다. Spring adapter에서는 `ApplicationEventPublisher`로도 내보낼 수
있지만, public handler 표면은 `ZLinkRuntimeEventHandler<T>`가 기준이다.

### 4.4 4단계: SPOT

Spot은 user Spot과 Entry Spot을 모두 지원해야 한다. `.NET`과 동일하게 SpotNode는
역할을 조합한다.

- router
- pub/sub
- attached channel client
- attached spot publisher client
- accepted route channel
- entry spot
- user spot factory

Spot timer는 일반 scheduler helper가 아니라 Spot lifecycle에 묶인 timer handle로
구현한다. user Spot timer callback은 같은 Spot 실행 경로 안에서 직렬화한다. Entry
Spot timer callback도 Entry Spot의 actor packet, lifecycle callback, request
continuation과 같은 직렬 실행 줄에서 실행한다.

### 4.5 5단계: STREAM session과 actor/session relay

STREAM은 header 기반 session 하나를 기준으로 구현한다. 이전 초안의 session type
분리 모델은 현재 `.NET` 계약과 맞지 않으므로 정식 포팅 기준에서 제외한다.

필수 기능은 아래와 같다.

- `addStreamNode(...).bind(...).attachActorGateway(...).registerSession(...)`
- `ZLinkSession` lifecycle callback
- `ZLinkSessionContext.client().send(...)`, `reply(...)`
- `ZLinkSessionContext.actors().bind(...)`, `find(...)`
- borrowed payload 규칙
- session별 serial dispatch queue
- ActorGateway를 통한 remote actor relay
- `ZLinkBoundSession` actor-to-client push와 disconnect

### 4.6 6단계: Stream Connector

client connector는 server framework와 별도 모듈로 둔다. `.NET`
`Systems.Zlink.Stream.Connector`와 같은 기능을 제공한다.

- endpoint, transport, timeout, heartbeat, reconnect option
- connect, close, dispatch
- send/request builder
- packet name resolver
- typed handler registry
- connection state event
- pending request tracking

Kotlin에서는 connector message stream을 `Flow`로 노출하되, 내부 receive loop는 Java
connector와 공유한다.

## 5. Spring Boot Adapter

Spring adapter는 runtime을 새로 정의하지 않는다. core module의 builder와 runtime을
Spring lifecycle에 연결한다.

| Spring 요소 | 책임 |
|-------------|------|
| `@EnableZLinkFramework` | auto configuration 활성화 |
| `ZLinkFrameworkConfigurer` | channel/spot/stream/actor/registry 설정 |
| `SmartLifecycle` | runtime start/stop (역순 종료 보장). `ApplicationRunner`는 one-shot readiness 신호 용도로만 |
| bean scanner | annotation handler와 interface handler 등록 |
| `ApplicationEventPublisher` | monitoring event 선택적 bridge |
| `@ConfigurationProperties` | endpoint, timeout, dispatch mode 기본값 |

handler class는 constructor injection으로 dependency를 받는다. context에서 Spring
`ApplicationContext`를 꺼내는 service locator 방식은 기본 사용법으로 두지 않는다.

DI에 노출되는 bean은 구성된 역할에 맞춘다. 없는 역할의 service를
무조건 등록하면 application이 잘못된 topology를 늦게 발견하므로 startup validation과
bean 등록 정책을 함께 둔다.

| 조건 | 노출 bean |
|------|-----------|
| channel client 역할 | `ZLinkClient` |
| fanout publisher 역할 | `ZLinkFanoutClient` |
| route mesh channel | `ZLinkRouteClient` |
| SpotNode + user Spot factory | `ZLinkSpotManager`, `ZLinkSpotClient` |
| SpotNode + actor factory | `ZLinkActorManager` |
| stream node + actor gateway | `ZLinkBoundSessionFactory` 내부 bean |
| embedded registry | `ZLinkRegistryQuery` |
| registry query client 설정 | `ZLinkRegistryQueryClient` |
| monitoring 설정 | `ZLinkRuntimeEventDispatcher`와 source polling lifecycle |

`ZLinkActorManager`는 actor factory가 없으면 등록하지 않는다. `ZLinkBoundSession`은
actor context에서 받는 표면이므로 application singleton bean으로 직접 노출하지 않는다.
Spring에서 connector client를 자동 bean으로 만드는 것은 첫 구현의 필수 기능이 아니다.
connector sample은 `ZLinkStreamConnectorFactory.create(...)`로 직접 생성한다.

## 6. Kotlin 표면

Kotlin은 Java contract를 감싸는 편의 계층이다. core runtime을 다시 만들지 않는다.
Java handler는 일반 함수처럼 값을 반환하거나 `void`로 끝난다. Kotlin adapter는
사용자가 작성한 `suspend` handler를 framework가 소유한 coroutine 안에서 실행하고,
완료된 뒤 Java handler와 같은 반환값 또는 예외로 core runtime에 전달한다.

```kotlin
suspend fun <TReply> ZLinkClient.request(
    channelName: String,
    request: Any,
    replyType: KClass<TReply>
): TReply

fun ZLinkFrameworkOptions.zlink(block: ZLinkFrameworkDsl.() -> Unit)
```

Kotlin DSL은 Java builder를 호출하는 thin wrapper다. Java와 다른 설정 의미를 만들지
않는다. 구현은 아래 규칙을 따른다.

- Kotlin adapter는 `GlobalScope`를 쓰지 않는다. framework host가 소유한
  `CoroutineScope`를 만들고, host shutdown 때 먼저 cancel한 뒤 Java runtime을
  닫는다. 이렇게 해야 진행 중인 handler가 host lifecycle 밖에 남지 않는다.
- 기본 dispatcher는 설정으로 받는다. 기본값은 application callback용 dispatcher이고,
  blocking 파일 I/O 같은 작업은 사용자가 별도 dispatcher를 명시해야 한다. zlink
  request 자체는 `CompletionStage.await()`로 suspend되므로 dispatcher thread를
  오래 점유하지 않는다.
- `suspend` request/send/publish helper는 Java builder의 `submit()`를 호출한 뒤
  `kotlinx-coroutines-jdk8`의 `await()`로 변환한다. Java call builder도
  `await(...)` 또는 `await()`를 제공해서 절차식 sample code가
  `toCompletableFuture().join()`을 직접 쓰지 않게 한다.
- `suspend` handler 등록은 Java handler interface로 변환한다. 변환된 handler는
  framework가 소유한 scope의 취소 상태를 같이 보면서 suspend 블록이 끝날 때까지
  기다린 뒤 값을 반환한다.
- channel, Spot, actor, session처럼 순서가 필요한 경로는 coroutine을 동시에 띄워
  순서를 맡기지 않는다. Java core의 serial execution queue가 이전 handler 실행이
  끝난 뒤 다음 dispatch를 시작한다.
- request timeout, session close, host shutdown은 Kotlin coroutine cancellation로
  전달되어야 한다. 반대로 coroutine이 cancel되면 handler 호출은 취소 예외로 끝나고
  pending reply 정리 정책을 따라야 한다.
- `suspend` handler가 던진 예외는 Java handler 예외와 같은 failure policy로 전달한다.
  reply error, monitoring event, retry 가능 여부는 Java core의 handler failure policy가
  한 곳에서 결정한다.
- Spring Boot adapter와 함께 쓸 때 MDC, tracing, security context 같은 thread-local
  값은 자동 보장을 전제로 하지 않는다. 필요한 경우 Kotlin adapter가 명시적 context
  propagation hook을 제공하고, 기본 동작은 문서화한다.
- Java handler와 Kotlin suspend handler가 같은 `kind + packetName`으로 등록되면
  중복 등록 오류로 처리한다. 언어가 다르다는 이유로 우선순위를 만들지 않는다.

## 7. 검증 계획

포팅이 가능한 수준의 첫 구현은 아래 순서로 검증한다.

1. contract compile: Java public API와 Kotlin extension compile
2. unit tests: handler scanner, packet name resolver, builder validator
3. fake backend tests: submit queue, reply correlation, session serial dispatch
4. Kotlin adapter tests: suspend handler completion, cancellation, exception mapping,
   duplicate registration, dispatcher 설정
4. native backend smoke: channel request/send, fanout, registry query
5. SPOT smoke: create/getOrCreate, route send/request, timer
6. STREAM smoke: session connected, dispatch, reply, close
7. actor/session E2E: local relay, remote ActorGateway relay, bound session push, disconnect
8. Spring Boot sample smoke: Bingo, TicTacToe session gateway에 대응하는 Java sample
9. connector smoke: TCP/WS send, request, manual dispatch, reconnect, codec roundtrip
10. sample regression: `samples/run_samples.sh`가 모든 필수 sample을 실행하고 self-check 통과

## 8. Behavior Matrix

Java/Kotlin 구현은 `.NET` behavior matrix와 같은 판정을 내려야 한다. 아래 항목은
문서에 별도로 예외가 적혀 있지 않으면 startup validation 오류다.

- duplicate channel name
- duplicate handler mapping: 같은 channel 안의 `kind + packetName`
- duplicate actor type factory
- duplicate Spot factory type
- duplicate Entry Spot registration
- outbound 역할에 discovery/manual 경로가 모두 없는 경우
- 같은 역할 안에서 discovery와 manual connection을 함께 쓰는 경우
- server/publisher/route/stream endpoint 누락
- stream node에 session type을 둘 이상 등록한 경우
- route mesh channel 없이 Registry-backed Spot remote address resolver를 쓰는 경우
- route mesh channel이 둘 이상인데 Registry-backed resolver의 router channel id를
  생략한 경우
- monitoring source 이름이 실제 runtime source와 맞지 않는 경우

다음은 startup 실패가 아니라 runtime event 또는 호출 실패로 다룬다.

- 이미 연결된 peer의 일시 disconnect
- discovery provider down
- polling source의 일시 query 실패
- stale bound session으로 보낸 actor push 실패
- request timeout

## 9. Lifecycle와 실패 의미

startup 순서는 아래와 같다.

1. registration surface 파싱
2. builder validation
3. Java binding context와 framework runtime 생성
4. embedded registry bind
5. channel, spot mesh, stream node 시작
6. monitoring source attach
7. Spring host ready

shutdown 순서는 반대 방향이다.

1. monitoring source detach
2. channel, spot mesh, stream node stop
3. embedded registry stop
4. Java binding context close

`send`, `publish`는 원격 handler 완료를 기다리지 않는다. framework가 transport에
submit할 수 있게 될 때까지만 비동기 대기한다. `request`는 packet submit과 reply
wait를 분리하고, reply wait는 request timeout 정책을 따른다.

STREAM session의 `onConnected()`는 connection ready 이후 호출한다.
`onError(...)`는 session과 매칭되는 transport error만 받는다. handshake 실패나
bind/accept/close 실패는 session callback이 아니라 monitoring event로 남긴다.

## 10. 구현 시 주의점

- Java framework는 Java binding의 public API만 호출한다.
- `.NET`에서 internal runtime helper였던 이름을 그대로 public으로 만들지 않는다.
- compatibility shim은 기본으로 만들지 않는다. 초기 포팅은 작고 명확한 public
  surface를 우선한다.
- request/send/publish submit은 thread를 오래 막지 않는 async submit 경로로 둔다.
- Java public API는 `submitAwait`, `awaitBlocking`처럼 별도 이름을 늘리지 않는다.
  대신 call builder에 `submit(...)`과 같은 작업을 기다리는 `await(...)` 또는
  `await()`를 함께 둔다. manager/context처럼 `CompletionStage`를 직접 반환하는 API는
  `ZLinkAwait.await(...)` helper로 절차식 sample code를 작성한다.
- Kotlin adapter는 Java runtime의 lifecycle, validation, ordering 의미를 바꾸지
  않는다. adapter 안에서 별도 mailbox, 별도 pending request tracker, 별도 retry
  policy를 만들지 않는다.
- STREAM inbound payload는 callback 동안 framework가 빌려준 값으로 취급한다.
- ActorGateway relay를 application route mesh packet으로 흉내 내지 않는다.
- Registry는 actor-session binding 저장소가 아니다.
- sample은 framework/connector public API만 사용한다.
- sample 통과를 위해 in-memory transport나 route replacement를 만들지 않는다.

## 11. 완료 판정 체크리스트

아래 항목이 모두 충족되어야 `.NET` framework와 같은 수준의 Java/Kotlin 포팅으로
본다.

- Java public interface catalog가 compile 가능한 형태로 구현되어 있다.
- Spring Boot starter가 channel, registry, monitoring, Spot, stream lifecycle을
  host lifecycle에 연결한다.
- builder validation이 이 문서의 behavior matrix와 topic 문서의 검증 기준을 모두
  반영한다.
- Java binding에 필요한 기능은 public API로 추가되어 있고, framework가 binding
  internal이나 reflection에 의존하지 않는다.
- client/server channel, fanout, route mesh channel이 fluent submit API를 제공한다.
- Registry는 embedded registry와 remote query client를 분리한다.
- Monitoring은 socket, discovery, registry, spot event를 typed handler로 올린다.
- SpotNode는 Entry Spot, user Spot factory, route egress, timer, actor dispatch를
  지원한다.
- STREAM은 header 기반 `ZLinkSession` 하나와 ActorGateway attach를 기준으로 동작한다.
- Stream Connector는 TCP/TLS/WS/WSS, codec helper, manual dispatch, reconnect,
  request timeout을 지원한다.
- Kotlin 모듈은 Java API 위의 coroutine/DSL wrapper로 동작하며 별도 runtime 의미를
  만들지 않는다.
- Kotlin `suspend` handler는 framework가 소유하는 `CoroutineScope`에서 실행되고,
  shutdown/cancellation/exception/serial ordering이 Java core와 같은 의미로 test된다.
- `samples/java/*`와 `samples/kotlin/*` 아래의 `TicTacToe`, `Bingo` sample이 실제
  connector와 framework public API만 사용해 self-check를 통과한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [Java 묶음](./README.ko.md) | [다음: Draft -- ZLink Framework Java Interface Catalog](./handler-interfaces.ko.md)
<!-- framework-adapter-nav:bottom:end -->
