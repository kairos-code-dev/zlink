<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework Node.js Behavior Matrix](./behavior-matrix.ko.md) | [다음: ZLink Framework Node.js Lifecycle And Failure Semantics](./lifecycle-and-failure-semantics.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[Node.js 묶음](../README.ko.md) | [표면 매핑 정책](./dotnet-to-node-surface-mapping.ko.md) | [인터페이스](../spec/handler-interfaces.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md) | [Lifecycle](./lifecycle-and-failure-semantics.ko.md)

# ZLink Framework Node.js DI Capability Exposure Policy

> 이 문서는 `Node.js` `ZLink Framework`(NestJS)에서 DI 로 노출되는 public service 표면을 어떤
> 역할 구성과 묶는지 정리하고, 현재 .NET 코드에 반영된 정책을 그대로 옮겨
> 기록한다. 개념·의미론은 .NET 과 동일하며, 표면(IServiceCollection → DynamicModule,
> constructor injection → provider token 주입)만 NestJS 로 바꾼다. 표기가 어긋나면
> `framework/languages/dotnet/src` 코드가 최종 기준이다.

## 1. 문제

현재 `ZLinkModule.forRoot(...)`(.NET `AddZLinkFramework(...)`) 는 여러 public service
를 항상 DI 에 등록하면 안 된다. 그러면 애플리케이션 코드는 어떤 기능을 등록하지
않았더라도 생성자 주입(provider token 주입)으로 해당 service 를 받을 수 있게 되기
때문이다.

이 방식은 channel client 처럼 여러 channel 이름 중 하나를 런타임에 고르는
표면에서는 어느 정도 허용할 수 있다. 그러나 Spot 과 Actor 계열에서는 문제가
크다. `SpotNode` 가 없는데 `ZLinkActorManager` 를 주입받아 actor 를 만들 수
있으면, 사용자는 actor 가 실제 actor system 안에 들어갔다고 이해하기 쉽다.
하지만 `SpotNode` 가 없으면 actor 를 붙일 node 가 없으므로 그 actor 는 의미 있는
위치를 갖지 못한다.

이 문서의 목표는 다음 질문에 답하는 것이다.

- 어떤 service 를 항상 DI 에 등록해도 되는가?
- 어떤 service 는 역할이 있을 때만 DI 에 등록해야 하는가?
- 어떤 조합은 startup validation 단계에서 바로 막아야 하는가?
- runtime 사용 시점에 실패해야 하는 경우라면 어떤 예외와 메시지를 써야 하는가?

## 1.1 결정 요약

| 결정 | 내용 |
|------|------|
| Spot service | `SpotNode` 가 있을 때만 DI 에 등록한다 |
| Actor manager | `SpotNode` 와 actor factory 가 모두 있을 때만 DI 에 등록한다 |
| Actor factory | `SpotNode` 없이 등록하면 startup validation 오류로 처리한다 |
| Bound session | actor bound session runtime만 DI 에 등록한다 |
| Route resolver | resolver 자체는 정책 객체이므로 단독 등록을 허용한다 |
| Channel client | target 이름을 호출 시점에 받으므로 항상 등록하되, target 누락은 configuration error 로 처리한다 |
| Missing proxy | public DI 표면에서는 제거하고, 조건을 만족하지 않으면 service provider 를 등록하지 않는다 |

## 2. 설계 원칙

### 2.1 주입 가능성은 기능 가능성을 암시해야 한다

사용자가 public service 를 생성자에서(`@Inject(token)`) 받았다면, 기본적으로 그
기능을 쓸 수 있다고 이해한다. 따라서 provider 등록은 단순 convenience 가 아니라
역할 계약의 일부로 본다.

예외는 multi-target client 이다. 예를 들어 `ZLinkChannelClient` 는 channel 이름을
인자로 받기 때문에 모든 channel 을 미리 알 수 없다. 이 경우 provider 자체는
등록할 수 있지만, 잘못된 channel 이름이나 역할 누락은 호출 시 명확한
configuration error 로 실패해야 한다.

### 2.2 Spot 과 Actor 는 SpotNode 에 묶인다

Actor 는 독립 객체가 아니라 `SpotNode` 에 속한 actor 이다. Actor 는 생성 직후
해당 node 의 Entry Spot 에 위치한다고 본다. 따라서 `SpotNode` 없이 actor 를
생성하거나 actor manager 를 유효한 service 처럼 노출하지 않는다.

Spot 도 마찬가지다. `ZLinkSpotManager` 는 Spot 을 생성하고 조회하는 표면이므로
`SpotNode` 가 없는 runtime 에서는 의미가 없다.

### 2.3 startup validation 이 우선이다

구성만 보고 잘못된 조합을 알 수 있으면 `ZLinkModule.forRoot(...)` validation 에서
실패시킨다. 사용자가 애플리케이션을 실행한 뒤 특정 코드 경로를 지나야 실패하는
방식은 피한다.

NestJS 에서 이 validation 은 `forRoot(...)` 의 `DynamicModule` 생성 시점이나
`forRootFactory(...)` 의 factory 결과가 registration 으로 변환되는 시점에 돈다.
따라서 잘못된 registration 자체는 runtime start 전에 throw 한다.

호출 시점에만 알 수 있는 오류는 `ZLinkConfigurationException` 또는
`ZLinkFrameworkException` 으로 명확하게 낸다. NestJS 의 일반 `Error` 만으로
역할 누락을 표현하지 않는다.

## 3. DI 노출 정책

NestJS DI 는 .NET 의 interface 등록 대신 **provider token**(`InjectionToken` /
`Symbol` / 문자열 토큰)으로 service 를 노출한다. `@Inject(TOKEN)` 으로 주입하며,
token 은 framework 가 export 한다. 아래 표는 .NET interface ↔ node provider token
↔ 구현 client 클래스의 대응을 함께 둔다.

### 3.1 항상 등록해도 되는 service

다음 service 는 framework runtime 전체의 기본 표면이거나, target 이름을 호출
시점에 받는 multi-target client 이므로 항상 등록해도 된다.

| provider token | 주입 타입 | 이유 | 역할 누락 시 동작 |
|----------------|-----------|------|------------------------|
| `ZLINK_CHANNEL_CLIENT` | `ZLinkChannelClient` | channel 이름을 호출 시점에 받는 outbound client | channel 이 없거나 client 역할이 없으면 호출 시 `ZLinkConfigurationException` |
| `ZLINK_ROUTE_CLIENT` | `ZLinkRouteClient` (`ZLinkMultipartRouteClient` 포함) | route channel id 를 호출 시점에 받는 outbound route client | route channel 이 없으면 호출 시 `ZLinkConfigurationException` |
| `ZLINK_FANOUT_CLIENT` | `ZLinkFanoutClient` | fanout channel 이름을 호출 시점에 받는 publisher | publisher 역할이 없으면 호출 시 `ZLinkConfigurationException` |
| `ZLINK_BOUND_SESSION_FACTORY` | `ZLinkBoundSessionFactory` | actor bound session factory | binding 없는 actor 에서 호출 시 `ActorSessionNotBound` |
| `ZLINK_MESSAGE_METADATA_POLICY` | `ZLinkMessageMetadataPolicy` | 메시지 metadata 복사 정책 | 항상 유효 |

Spot routed egress 는 별도 public DI client 로 노출하지 않는다. current Spot
callback 안에서는 `ZLinkSpotOutbound` 가 Spot outbound 를 담당하고, callback 밖의
application 코드는 actor 생성 또는 Entry Spot join 같은 도메인 흐름으로
`ActorRef` 를 얻어 session actor handle 에 bind 한다.

위 service 는 항상 주입 가능하더라도, 내부에서 없는 channel 을 자동으로 만들면
안 된다. 없는 channel 또는 역할은 즉시 설정 오류로 처리한다.

### 3.2 역할이 있을 때만 등록하는 service

다음 service 는 특정 runtime 역할이 없으면 기능 자체가 성립하지 않는다.
따라서 해당 역할이 등록된 경우에만 provider 를 module 의 `providers` /
`exports` 에 추가한다.

| provider token | 주입 타입 | 등록 조건 | 등록하지 않을 때 |
|----------------|-----------|-----------|------------------|
| `ZLINK_SPOT_MANAGER` | `ZLinkSpotManager` | 최소 1개 이상의 `SpotNode` | DI resolve 실패 (`UnknownDependenciesException`) |
| `ZLINK_SPOT_OUTBOUND` | `ZLinkSpotOutbound` | 최소 1개 이상의 `SpotNode` | DI resolve 실패 |
| `ZLINK_SPOT_PUBLISHER_CLIENT` | `ZLinkSpotPublisherClient` | 최소 1개 이상의 Spot publisher client 역할(attached spot publisher client) | DI resolve 실패 |
| `ZLINK_ACTOR_MANAGER` | `ZLinkActorManager` | 최소 1개 이상의 `SpotNode` **와** 최소 1개 이상의 actor factory | DI resolve 실패 |

이 정책은 사용자가 잘못된 기능을 생성자에서 바로 요구했을 때, 해당 token 이
등록되어 있지 않다는 사실을 NestJS DI 가 일관되게 알려 주도록 만들기 위한 것이다.
NestJS 는 등록되지 않은 token 을 주입받는 provider 가 있으면 부팅(컨테이너 구성)
단계에서 `UnknownDependenciesException` 으로 실패하므로, .NET 의 service provider
validation 과 동일하게 시작 단계에서 잡힌다.

`forRootFactory(...)` 와 handler discovery 를 쓰는 `forRoot(...)` 는 예외다.
NestJS 는 async factory 결과나 `DiscoveryService` 기반 registration 결과를 받기
전에 `DynamicModule` 의 provider 목록을 확정해야 하므로, 최종 registration 에 따라
provider 자체를 제거할 수 없다. 이 경로들은 위 역할 token 을 export 하되,
registration 에 역할이 없으면 provider 값으로 `null` 을 돌려 application
context 부팅을 유지한다. 사용자는 이 구성에서 optional 역할을 주입할 때
`null` 가능성을 명시적으로 처리해야 한다.

### 3.3 구성에 따라 등록하는 service

다음 service 는 이미 특정 구성과 강하게 묶여 있다. 이 정책을 유지하되, missing
proxy 를 등록하는 방식은 줄인다.

| provider token | 주입 타입 | 등록 조건 | 권장 동작 |
|----------------|-----------|-----------|-----------|
| `ZLINK_BOUND_SESSION_FACTORY` | `ZLinkBoundSessionFactory` | framework runtime | 항상 등록 |
| `ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER` | `ZLinkSpotRemoteAddressResolver` | `spot.remoteAddressResolver`(`AddSpotRemoteAddressResolver<TResolver>()` 대응) 또는 registry remote address 구성 | 조건을 만족할 때만 등록 |

기존 missing proxy 는 사용 시점까지 오류를 늦춘다. bound session factory 는
항상 등록하고, 현재 actor 에 묶인 session binding 이 없을 때 호출 지점에서
`ActorSessionNotBound` 로 실패한다.

- session actor dispatch 구성을 쓰는데 route mesh channel 이 없으면 validation
  오류로 낸다.

## 4. startup validation 규칙

### 4.1 Actor factory 와 SpotNode

`.actorFactory(...)` 로 actor factory 를 하나라도 등록했다면(.NET
`AddActorFactory<TFactory>(actorType)` 대응) 최소 1개 이상의 `SpotNode` 가
필요하다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .actorFactory('player', PlayerActorFactory)
    .addSpotNode('play-node')
      .enablePubSub('tcp://127.0.0.1:9000')
      .addEntrySpot(GameEntrySpot)
    .build()
);
```

`SpotNode` 없이 actor factory 만 등록하면 다음 오류로 실패한다.

```text
ZLinkConfigurationException:
Actor factory registration requires at least one SpotNode.
```

### 4.2 Spot remote address resolver

`spot.remoteAddressResolver`(`AddSpotRemoteAddressResolver<TResolver>()` 대응)
자체는 `SpotNode` 를 직접 요구하지 않는다. resolver 는 `spotRid` 또는 `spotId` 를
route 로 바꾸는 정책 객체일 뿐이다. session gateway 나 API 서버가 route 정보를
저장하거나 전달하기 위해 같은 resolver 구현을 등록할 수 있다.

다만 `ZLinkSpotOutbound` 는 local spot 실행 문맥을 전제로 하므로 `SpotNode` 가
있을 때만 DI 에 등록한다. 따라서 `SpotNode` 없이 resolver 만 등록한 구성에서는
resolver 는 주입 가능하지만 `ZLinkSpotOutbound` 는 주입되지 않는다.

이 구분이 필요한 이유는 서버 역할이 서로 다를 수 있기 때문이다. Play 서버처럼
local `SpotNode` 를 띄우는 서버는 `ZLinkSpotOutbound` 와 resolver 를 함께 사용할 수
있다. 반면 session gateway 서버처럼 route 정보를 저장하거나 전달만 하는 서버는
resolver 구현을 DI 로 제공할 수 있지만, local spot 문맥이 없으므로
`ZLinkSpotOutbound` 를 주입받으면 안 된다.

> 참고: registry 기반 remote address resolver 를 쓰는 경우(.NET
> `RegistrySpotRemoteAddresses`), validation 은 추가로 route mesh channel
> (`.addRouteMeshChannel(...)`)과 discovery endpoint(`.useDiscovery()`)를 요구한다. 둘 중 하나라도 없으면
> `ZLinkConfigurationException` 으로 실패한다.

### 4.3 Spot publisher client

`ZLinkSpotPublisherClient`(`ZLINK_SPOT_PUBLISHER_CLIENT`)는 attached Spot
publisher client 역할이 있을 때만 등록한다.

Spot publisher client 역할 없이 외부 publish service 를 주입받고 싶다면,
그 애플리케이션은 일반 fanout publisher 인 `ZLinkFanoutClient` 를 써야 한다.
두 표면은 같은 publish 동작처럼 보일 수 있지만, 전자는 Spot mesh 의 attached
publisher client 를 전제로 하고 후자는 일반 channel publisher 를 전제로 한다.

### 4.4 Bound session

`ZLinkBoundSessionFactory`(`ZLINK_BOUND_SESSION_FACTORY`)는 framework runtime 과
함께 등록한다. bound session 은 actor runtime state 에 저장된 현재 session rid 와
binding token 을 사용한다. binding 이 없는 actor 에서 호출하면
`ActorSessionNotBound` 로 실패한다.

```text
ZLinkFrameworkException:
ActorSessionNotBound
```

## 5. 호출 시점 오류 규칙

항상 등록되는 multi-target client 는 호출 인자로 역할을 선택한다. 이 경우
provider 등록 시점에는 정확한 대상이 없으므로 호출 시점 오류를 허용한다. 단, 오류는
configuration error 로 표현한다.

| 호출 | 오류 조건 | 예외 |
|------|-----------|------|
| `ZLinkChannelClient.requestToChannel(channelName, ...)` | channel 이 없거나 client 역할이 없음 | `ZLinkConfigurationException` |
| `ZLinkChannelClient.sendToChannel(channelName, ...)` | channel 이 없거나 client 역할이 없음 | `ZLinkConfigurationException` |
| `ZLinkFanoutClient.publishToChannel(channelName, ...)` | channel 이 없거나 publisher 역할이 없음 | `ZLinkConfigurationException` |
| `ZLinkRouteClient.send(routerChannelId, ...)` | route mesh channel 이 없음 | `ZLinkConfigurationException` |

역할 누락은 NestJS 일반 `Error` 가 아니라 위 예외로 처리한다.

## 6. 구현 반영 항목

이 정책은 다음 코드 경로에 반영한다(.NET 대응 경로는 괄호로 표기).

1. `ZLinkFrameworkRegistrationValidator`(.NET `ZLinkFrameworkRegistrationValidator`)
   에 역할 validation 을 둔다. `forRoot` 는 `DynamicModule` 을 만들기 전에
   호출하고, `forRootFactory` 는 NestJS 가 factory 를 실행한 뒤 호출한다.
2. framework module factory(.NET `ZLinkFrameworkServiceRegistrar.AddPublicClients(...)`)
   에서 public service provider 를 역할 조건에 따라 `providers` / `exports`
   에 넣는다.
3. missing bound session factory/service 는 public DI 표면에서 제거한다.
4. channel / route / publisher runtime lookup 실패는 `ZLinkConfigurationException`
   으로 정리한다(.NET `ZLinkChannelRuntimeManager`,
   `ZLinkFrameworkRuntimeChannels` 대응).
5. public token catalog 에 DI 등록 조건을 함께 둔다.

## 7. 회귀 테스트

다음 회귀 테스트로 정책 반영 여부를 확인한다. NestJS 에서는 `Test.createTestingModule`
로 module 을 컴파일한 뒤 `module.get(TOKEN)`(또는 부팅 시 throw 여부)으로 확인한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `NodesAndServices.forRoot_Throws_When_ActorFactory_Without_SpotNode` | actor factory 만 등록하면 startup validation 이 실패한다 |
| `NodesAndServices.forRoot_DoesNot_Register_ActorManager_Without_SpotNode` | SpotNode 없는 구성에서는 `ZLINK_ACTOR_MANAGER` 가 DI 에 없다 |
| `NodesAndServices.forRoot_DoesNot_Register_ActorManager_With_SpotNode_Only` | SpotNode 만 있고 actor factory 가 없으면 `ZLINK_ACTOR_MANAGER` 가 DI 에 없다 |
| `NodesAndServices.forRoot_DoesNot_Register_SpotServices_Without_SpotNode` | SpotNode 없는 구성에서는 Spot service 가 DI 에 없다 |
| `NodesAndServices.forRoot_Registers_SpotServices_When_SpotNode_Exists` | SpotNode 가 있으면 Spot service 가 DI 에 등록된다 |
| `NodesAndServices.forRoot_Registers_ActorManager_When_SpotNode_And_ActorFactory_Exist` | SpotNode 와 actor factory 가 있으면 `ZLINK_ACTOR_MANAGER` 가 등록된다 |
| `NodesAndServices.forRoot_DoesNot_Register_SpotPublisher_Without_PublisherCapability` | SpotNode 가 있어도 publisher 역할이 없으면 Spot publisher service 는 DI 에 없다 |
| `NodesAndServices.forRoot_Registers_SpotPublisher_When_PublisherCapability_Exists` | Spot publisher 역할이 있으면 Spot publisher service 가 DI 에 등록된다 |
| `NodesAndServices.forRoot_Registers_BoundSession_Factory` | bound session factory 는 framework runtime 과 함께 등록된다 |
| `NodesAndServices.forRoot_Allows_SpotRemoteAddressResolver_Without_SpotNode` | remote address 정보만 제공하는 서버는 SpotNode 없이 `ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER` 를 등록할 수 있다 |
| `NodesAndServices.forRoot_DoesNot_Register_SpotOutbound_With_Resolver_Only` | Spot remote address resolver 만 있고 SpotNode 가 없으면 `ZLINK_SPOT_OUTBOUND` 는 DI 에 없다 |
| `HandlerExposure.RouteClient_Throws_ConfigurationException_When_RouteChannel_Missing` | route channel 누락 오류가 configuration error 로 나온다 |
| `HandlerExposure.ChannelClient_Throws_ConfigurationException_When_ClientCapability_Missing` | channel client 역할 누락 오류가 configuration error 로 나온다 |

## 8. 적용 후 기대 상태

이 정책을 적용하면 사용자는 생성자 주입(`@Inject(token)`)만 보고 기능 사용 가능
여부를 더 쉽게 판단할 수 있다.

- SpotNode 없는 애플리케이션은 Spot service 를 주입받지 못한다.
- actor manager 는 SpotNode 와 actor factory 가 모두 있을 때만 주입받을 수 있다.
- actor factory 를 등록했다면 반드시 SpotNode 도 등록해야 한다.
- bound session 는 현재 actor 에 session binding 이 있을 때만 유효하다.
- multi-target channel client 는 항상 등록할 수 있지만, 없는 대상은 명확한
  configuration error 로 실패한다.

결과적으로 "주입은 되지만 실제로는 쓸 수 없는 service"를 줄이고, 잘못된 구성은
가능한 한 애플리케이션 시작(module 부팅) 단계에서 드러낸다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework Node.js Behavior Matrix](./behavior-matrix.ko.md) | [다음: ZLink Framework Node.js Lifecycle And Failure Semantics](./lifecycle-and-failure-semantics.ko.md)
<!-- framework-adapter-nav:bottom:end -->
