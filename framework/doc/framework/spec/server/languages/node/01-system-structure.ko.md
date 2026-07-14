<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:end -->

[Node spec 목차](README.ko.md)

# Node 시스템 구조 — 패키지, 등록과 부트스트랩

> 이 문서는 **NestJS 위에서 ZLink framework를 어떻게 구성하는가**를 소유한다. 패키지 구조, 배포,
> 모듈 부트스트랩, DI, lifecycle, 그리고 각 기능의 **등록 표면**이다.
>
> **기능의 의미와 동작 규칙은 공통 스펙이 소유한다** — [channel-messaging](../../11-channel-messaging.ko.md),
> [spot-messaging](../../20-spot-messaging.ko.md), [spot-node](../../21-spot-node.ko.md),
> [stream-session](../../30-stream-session.ko.md), [actor-model](../../22-actor-model.ko.md),
> [session-actor-dispatch](../../31-session-actor-dispatch.ko.md),
> [runtime-monitoring](../../50-runtime-monitoring.ko.md),
> [location-runtime](../../40-location-runtime.ko.md),
> [channel-topology](../../10-channel-topology.ko.md).
>
> **public 타입과 시그니처는 [handler-interfaces](02-handler-interfaces.ko.md)가 소유한다.**
> **사용 예제와 튜토리얼은 [Node 가이드](../../../../node/guide/01-overview.ko.md)가 소유한다.**
> client connector는 [stream-connector](../../../stream-connector/languages/typescript/03-stream-connector.ko.md)가 소유한다.

## 1. 패키지 구조

| package | 역할 | 의존 |
|---|---|---|
| `@zlink-systems/framework` | framework core — contract, runtime, dispatcher | `zlink`, `stream-wire`, OpenTelemetry API |
| `@zlink-systems/nestjs` | NestJS host adapter — `ZLinkModule.forRoot(...)` 등록 표면 | `framework`, NestJS common/core, `reflect-metadata`, `rxjs` |
| `@zlink-systems/framework-codec-protobuf` | Protobuf codec **extension** | `framework`, `stream-connector`, `protobufjs` |
| `@zlink-systems/framework-codec-msgpack` | MessagePack codec **extension** | `framework`, `stream-connector`, `@msgpack/msgpack` |
| `@zlink-systems/framework-locations-redis` | Redis location store **extension** | `framework`, `zlink`, `redis` |
| `@zlink-systems/http-client` | fluent HTTP/JSON client | `framework`, `undici` |
| `@zlink-systems/stream-connector` | **client** connector — 서버 framework에 의존하지 않는다 | `stream-wire` |
| `@zlink-systems/stream-wire` | connector와 서버가 공유하는 **wire 계층** | 없음 |

**분리 원칙:**

- **codec 구현을 core에 섞지 않는다.** JSON은 기본 codec이고, Protobuf·MessagePack은
  extension package로 분리한다. 현재 Node HTTP client는 framework codec registry를
  사용하지 않는다. codec 공유 범위의 공통 계약은
  [channel-messaging §6](../../11-channel-messaging.ko.md)을 따른다.
- **location store 구현도 extension이다.** core는 store 계약만 알고 Redis 구현은 별도 package가
  제공한다(§10).
- **connector는 서버 framework package를 참조하지 않는다.** 반대 방향도 같다.
- **`stream-wire`는 환경 중립이다.** `Uint8Array`만 사용하고 `Buffer`에 의존하지 않으므로 Node와
  브라우저에서 **같은 코드**로 동작한다.
- **host adapter(`nestjs`)와 core를 나눈다.** core는 NestJS에 의존하지 않는다.

## 2. 배포 계획

| package | 배포 채널 | 소비자 |
|---|---|---|
| `@zlink-systems/framework` · `@zlink-systems/nestjs` | npm | 서버 애플리케이션 |
| `@zlink-systems/framework-codec-*` | npm | codec이 필요한 서버·브라우저 client |
| `@zlink-systems/framework-locations-redis` | npm | 다중 프로세스 배포 |
| `@zlink-systems/stream-connector` | npm | 브라우저 계열 client |
| `@zlink-systems/stream-wire` | npm | connector와 서버가 공유 |

**TypeScript connector는 package root 하나를 ESM으로 배포한다.** 이 진입점은 브라우저 계열
client에서 플랫폼 `WebSocket`을 사용한다. Node.js에서 실행하는 connector와 별도 browser
subpath는 제공하지 않는다. 정확한 계약은
[TypeScript Stream Connector](../../../stream-connector/languages/typescript/03-stream-connector.ko.md)가 소유한다.

## 3. 모듈 부트스트랩

`ZLinkModule.forRoot(...)` / `forRootFactory(...)`가 등록 진입점이다.

**`forRoot(...)`은 transport·node·역할·handler group 선택을 선언하는 자리다. application 객체
그래프를 조립하는 자리가 아니다.**

## 4. DI

- framework가 노출하는 outbound client와 manager는 **NestJS provider token**으로 등록한다.
  `@Inject(TOKEN)`으로 받으며 token은 framework가 export한다.
- **handler는 context의 service locator가 아니라 생성자 주입으로 의존을 받는다.**
  **context에 DI 컨테이너를 넣지 않는다.**
- application이 구현하는 객체는 **NestJS DI 컨테이너가 소유한다.** 부트스트랩 코드에서 직접
  `new`로 만들지 않고 module `providers`에 등록한다.

| 객체 | 등록 | framework가 resolve하는 시점 |
|---|---|---|
| channel/fanout/route handler | `providers` + handler 등록 표면 | channel이 그 handler group을 dispatch할 때 |
| Entry Spot, user Spot | `providers` + `addEntrySpot(...)` / `addSpotFactory(...)` | SpotNode·SpotManager가 spot을 활성화할 때 |
| Spot packet·subscribe·actor·timer handler | handler decorator + `zlinkDiscoverProviders(...)` | 그 Spot 실행 문맥에서 처리할 때 |
| actor factory | `providers` + SpotNode `actorFactory(...)` | ActorManager가 actor를 생성할 때 |
| stream session(또는 factory) | `providers` + `streams` 설정 | stream 연결을 session으로 활성화할 때 |

### 4.1 Provider token

**주입에 쓰는 token 심볼은 framework가 export한다.**

**항상 등록되는 provider:**

| token | 표면 |
|---|---|
| `ZLINK_CHANNEL_CLIENT` | channel client |
| `ZLINK_ROUTE_CLIENT` | route client |
| `ZLINK_FANOUT_CLIENT` | fanout client |
| `ZLINK_BOUND_SESSION_FACTORY` | bound session factory |
| `ZLINK_RUNTIME_EVENT_PUBLISHER` | runtime event publisher |
| `ZLINK_DRAIN_CONTROL` | graceful drain control |
| `ZLINK_CHANNEL_RUNTIME_OPTIONS` | channel runtime options |
| `ZLinkDrainHealthIndicator` | drain readiness와 health indicator |
| `ZLINK_MESSAGE_METADATA_POLICY` | metadata 정책 |
| `ZLINK_FRAMEWORK_RUNTIME` · `ZLINK_FRAMEWORK_REGISTRATION` | runtime과 등록 |

**역할이 있을 때만 등록되는 provider:**

| token | 필요한 역할 |
|---|---|
| `ZLINK_SPOT_MANAGER` · `ZLINK_SPOT_OUTBOUND` | spot mesh 등록 |
| `ZLINK_SPOT_PUBLISHER_CLIENT` | spot publisher 역할 |
| `ZLINK_ACTOR_CLIENT` | Spot node와 location store가 모두 등록됨 |
| `ZLINK_ACTOR_MANAGER` | actor manager가 활성화됨 |
| `ZLINK_SPOT_HANDLE_RESOLVER` · `ZLINK_ACTOR_SPOT_HANDLE_RESOLVER` | location store가 하나 이상 등록됨 |
| `ZLINK_LOCATION_RUNTIME_QUERY` | location store가 하나 이상 등록됨 |

**등록되지 않은 token을 주입하면 NestJS의 미해결 의존성 오류로 실패한다.**

> **`forRoot`와 `forRootFactory`의 실패 모양이 다르다.** 정적 `forRoot`에서 역할이 없으면
> **provider 자체가 등록되지 않는다.** `forRootFactory`처럼 동적으로 구성하는 경로에서는 역할이
> 없을 때 **provider 값이 `null`이 될 수 있다.** 주입 지점에서 두 경우를 구분해 다뤄야 한다.

**decorator의 책임 분리:**

- **channel handler**는 decorator로 group 이름을 붙이고, **channel이 그 group을 선택한다.**
- **Spot actor handler**는 decorator로 대상 Spot 타입을 명시한다.
- **Spot timer handler**도 decorator로 표시하고, module이 `zlinkDiscoverProviders(...)`로 수집한다.

**이렇게 나눠야 "channel이 어떤 handler 묶음을 받을지"와 "Spot·session이 자기 내부 메시지를 어떻게
처리할지"가 섞이지 않는다.**

## 5. Lifecycle

NestJS provider lifecycle hook에 runtime을 배선한다.

| hook | 시점 |
|---|---|
| `onModuleInit()` | **모든 provider가 DI에서 resolvable해진 뒤** runtime 시동(bind·connect·discovery) |
| `onModuleDestroy()` | 별도 shutdown hook을 실행하지 않는면 no-op |
| `onApplicationShutdown()` | drain을 실행하고 runtime 자원을 정리 |

**`onModuleInit()`에서 시동하는 이유는 socket bind/connect와 discovery가 시작되려면 handler
provider가 모두 resolvable해야 하기 때문이다.**

### 5.1 시동 순서

lifecycle 참여자는 **framework → monitoring** 순서다.

1. backend channel adapter로 context를 생성한다.
2. Spot node를 시작하고 route mesh router를 bind한다.
3. location runtime과 자동 연결을 준비한다.
4. channel receive loop와 stream node를 시작한다.
5. monitoring source를 준비된 runtime에 attach한다.

**시동은 idempotent해야 한다.** monitoring hook이 같은 runtime을 다시 시동시켜도 두 번 시작되지
않는다.

### 5.2 종료 순서

shutdown은 stop signal을 먼저 전달한 뒤 소유자별로 정리한다.

1. monitoring source를 detach한다.
2. stream, Spot, channel runtime을 순서대로 dispose한다.
3. location lifecycle과 location runtime을 정리한다.
4. listener task가 종료되는 것을 기다린다.
5. runtime state와 backend context를 마지막에 dispose한다.

### 5.3 fail-fast

**startup에서 runtime state를 만들다 한 컴포넌트라도 실패하면, 그때까지 만든 state를 그 자리에서
dispose한 뒤 예외를 다시 던진다.** 반쯤 열린 socket이나 매달린 context를 남기지 않는다.

내부 정리 순서는 [runtime-lifecycle](../../../../node/internals/runtime-lifecycle.ko.md)이,
backend 어댑터 포트는
[backend-dependency-policy](../../../../node/internals/backend-dependency-policy.ko.md)가 소유한다.

## 6. Channel 등록

`zlinkFramework()` fluent builder로 선언한다.

| 역할 | 의미 | bind |
|---|---|---|
| `enableServer(...)` | 이 channel로 들어오는 request/send를 local handler가 받는다 | **필요** |
| `enableClient(...)` | 이 channel 쪽으로 request/send를 내보낸다 | 불필요 |
| `enablePublisher(...)` | 이 channel로 event를 publish한다 | **필요** |
| `enableSubscriber(...)` | 이 channel의 event를 받는다 | 불필요 |

자동·수동 연결, dispatch key와 중복 검사 범위는
[channel-topology §5](../../10-channel-topology.ko.md)와
[channel-messaging](../../11-channel-messaging.ko.md)이 소유한다.

## 7. SPOT 등록

**`addSpotMesh(channelName)` 한 번이 SPOT channel 이름과 그 channel을 소유하는 `SpotNode` 하나를
함께 등록한다.** discovery는 등록된 location store를 사용한다.

| builder | 켜는 것 |
|---|---|
| `enableRouter(endpoint, routingId?)` | spot router 역할 |
| `enablePubSub(endpoint, routingId?)` | spot pub/sub 역할 |
| `configureEntrySpot({ routingId })` | native Entry Spot facade 설정 |
| `addEntrySpot(TEntrySpot)` | Entry Spot handler registry 타입 |
| `addSpotFactory(TSpot)` | 이 노드가 만들 수 있는 spot 타입 |
| `addClientServerChannel(...).enableClient(...)` | SPOT handler의 channel send/request가 공유하는 client |

중복 등록, route bridge와 타입 규칙은
[spot-node](../../21-spot-node.ko.md)와 [spot-messaging](../../20-spot-messaging.ko.md)이 소유한다.

### 7.1 Entry Spot routing id의 적용 순서

**Entry Spot routing id는 native SpotNode가 bind되기 전에 적용해야 한다.** core가 bind 이후 변경을
잠근다([spot-node §2.1](../../21-spot-node.ko.md)).

1. backend 어댑터가 native Entry Spot facade를 얻는다.
2. `routingId`가 설정되어 있으면 facade에 적용한다.
3. SpotNode를 bind한다.
4. discovery, route channel, publisher를 붙인다.
5. Entry Spot activation과 dispatch pump를 붙인다.

**native facade 호출은 backend 어댑터 내부에서만 일어난다. public surface에 바인딩 객체를 노출하지
않는다.**

Route ingress 규칙은 [spot-messaging §6](../../20-spot-messaging.ko.md)이 소유한다. 수동 outbound
peer는 route mesh builder의 `connect(...)`로 지정한다.

## 8. STREAM 등록

- **decorator 기반 암시 등록으로 열지 않는다.** `streams` 설정의 명시 등록만 기본 표면이다.
- **한 stream node에는 session을 하나만 둔다.**
- **bind endpoint는 반드시 있어야 한다.**
raw stream의 `write(...)`, `close(...)` 시그니처는
[02 인터페이스](02-handler-interfaces.ko.md)가 소유하고, backpressure 의미는
[stream-session](../../30-stream-session.ko.md)이 소유한다.

## 9. Session actor dispatch 등록

계약은 [session-actor-dispatch](../../31-session-actor-dispatch.ko.md)가 소유한다.

| 표면 | 역할 |
|---|---|
| STREAM session relay | **router 역할을 켠 SpotNode를 relay ingress로 자동 사용한다**(별도 지정 없음) |
| spot handle resolver | spot rid를 user Spot routing id로 푼다. actor가 node 경계를 넘을 수 있으면 등록한다 |

## 10. Monitoring · Location 등록

계약은 [runtime-monitoring](../../50-runtime-monitoring.ko.md)과
[location-runtime](../../40-location-runtime.ko.md)이 소유한다.

| 대상 | 등록 조건 |
|---|---|
| socket source | 이름이 `<channel>.<capability>` 형식이고 **그 channel 역할이 등록되어 있어야 한다** |
| location source | **polling 주기를 반드시 명시한다.** location runtime이 등록되어 있어야 한다 |
| spot source | **등록된 `SpotNode` 이름**을 가리켜야 한다 |
| location store | **물리 저장소 인스턴스 하나**를 등록 루트에서 **한 번만** 둔다. 메모리 store와 함께 등록하면 설정 오류다 |

**임의 source 자동 발견은 지원하지 않는다.**

Redis store는 `@zlink-systems/framework-locations-redis`가 제공한다(§1).

## 11. Startup validation

검증 항목의 정본은 [channel-messaging §4](../../11-channel-messaging.ko.md)와
[spot-messaging §8](../../20-spot-messaging.ko.md)이 소유한다.

**Node는 모든 위반을 startup 시점 설정 예외로 던진다.** 설정 실수를 즉시 드러내는 쪽이 기본
규칙이다.

## 12. 회귀 테스트

등록과 startup validation의 회귀 항목은
[regression-test-matrix](../../../../node/internals/regression-test-matrix.ko.md)가 소유한다.
