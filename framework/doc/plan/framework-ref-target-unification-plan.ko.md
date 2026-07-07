# Framework ActorRef / SpotRef 전송 대상 통일 계획

작성일: 2026-07-07

이 문서는 framework의 actor/spot 전송 대상 개념을 모든 언어에서 같은 방식으로 보이게
정리하기 위한 구현 계획이다. 현재 구현은 actor에는 `ActorRef` 계열 이름을 쓰고, spot에는
`SpotAddress` 계열 이름을 쓴다. 사용자는 메시지를 보낼 때 둘 다 "전송 대상 handle"로
이해하므로, public API에서는 `ActorRef`와 `SpotRef`로 맞춘다.

이 문서는 구현 전 계획이다. 정식 spec 문서와 각 언어 public contract는 이 계획에 따라
구현과 테스트가 끝난 뒤 함께 갱신한다.

## 1. 목표

- 모든 framework 언어에서 actor 전송 대상은 `ActorRef`, spot 전송 대상은 `SpotRef`로 부른다.
- actor/spot 메시징 API는 각각 `ActorRef`, `SpotRef`를 인자로 받는다.
- actor id 또는 spot id만 받아서 메시지를 보내는 API는 제거한다. id 기반 조회와 전송은
  분리한다.
- 사용자가 spot rid만 넘기면 framework가 매번 location store를 조회하는 API는 만들지 않는다.
- 사용자가 `Address`, `RemoteAddress`, `Ref` 중 무엇을 써야 하는지 고민하지 않도록 guide와
  public contract를 정리한다.
- C++, Node, Java, Kotlin, .NET framework가 같은 개념과 같은 사용 흐름으로 동작해야 한다.

## 2. 결정 사항

### 2.1 public 개념

| 대상 | public 이름 | 의미 |
|------|-------------|------|
| actor | `ActorRef` | actor id, owner node, generation을 포함하는 actor 전송 대상 handle |
| spot | `SpotRef` | spot owner node와 spot rid를 포함하는 spot 전송 대상 handle |

`SpotRef`는 actor와 같은 generation 의미를 가지지 않는다. 이름을 맞추는 이유는 내부 생명주기를
같게 만들기 위해서가 아니라, 사용자가 메시징 API를 같은 방식으로 쓰게 하기 위해서다.

### 2.2 id-only 메시징 API 제거

id는 조회 입력이고, ref는 전송 입력이다. 이 둘을 public API에서 섞으면 전송 경로가 location store
조회, stale 처리, 재조회 정책을 몰래 수행하게 된다. 대량 전송에서는 매 메시지마다 조회가 발생할 수
있고, 실패 원인도 "조회 실패"인지 "전송 실패"인지 흐려진다.

따라서 다음 규칙을 모든 언어에 적용한다.

- actor 생성·조회·ensure API는 actor id를 받을 수 있다. 이 API는 `ActorRef`를 반환한다.
- spot 조회 API는 spot id를 받을 수 있다. 이 API는 `SpotRef`를 반환한다.
- actor 메시징 API는 actor id를 받지 않는다. 반드시 `ActorRef`를 받는다.
- spot 메시징 API는 spot id를 받지 않는다. 반드시 `SpotRef`를 받는다.
- node rid와 spot rid를 낱개로 받는 spot 전송 API도 제거한다. 순서 실수를 막기 위해 `SpotRef`
  값 하나만 받는다.
- actor context의 user spot join API도 id-only 메시징과 같은 위험이 있는지 별도 판단한다. join은
  단순 메시지 전송이 아니라 actor 이동/admission workflow이므로 제거 여부를 메시징 API 제거와
  묶어 자동 결정하지 않는다. 다만 public 문서에서는 `JoinSpot(spotRid, ...)`가 일반 spot
  메시징 API가 아니라 lifecycle workflow임을 분명히 구분한다.

### 2.3 기존 이름 처리

| 기존 이름 | 처리 |
|-----------|------|
| `ZLinkSpotAddress`, `spot_address_t`, `ZLinkSpotAddress` interface | `SpotRef` 계열 이름으로 변경 |
| `IZLinkSpotAddressResolver`, `ZLinkSpotAddressResolver`, `spot_address_resolver_t` | service 역할 이름은 `IZLinkSpotRefResolver`/`ZLinkSpotRefResolver`처럼 `ZLink` prefix를 유지한다. C++은 namespace 관례에 맞춰 `spot_ref_resolver_t`로 변경 |
| `ResolveSpotAddress*` | `ResolveSpotRef*`로 변경 |
| `ResolveActorSpotAddress*` | `ResolveActorSpotRef*`로 변경 |
| `SendToSpot(... SpotAddress ...)` | `SendToSpot(... SpotRef ...)`로 변경 |
| `RequestToSpot(... SpotAddress ...)` | `RequestToSpot(... SpotRef ...)`로 변경 |
| `ZLinkSpotRemoteAddress`, `SpotRemoteAddressResolver` | 일반 application surface에서 제거하거나 internal/advanced routing extension으로 축소 |
| `SendToActor(actorId, ...)`, `RequestToActor(actorId, ...)` | 제거. 먼저 actor directory/manager에서 `ActorRef`를 얻은 뒤 전송 |
| `SendToSpot(spotRid, ...)`, `RequestToSpot(spotRid, ...)` | 제거. 먼저 spot resolver에서 `SpotRef`를 얻은 뒤 전송 |
| `SendToSpot(nodeRid, spotRid, ...)`, `RequestToSpot(nodeRid, spotRid, ...)` | 제거. 두 id를 낱개로 받지 않고 `SpotRef` 값 하나를 받음 |

호환성 alias는 기본 계획에 포함하지 않는다. 이 저장소는 아직 public contract를 맞추는 단계이므로,
같은 의미의 public 이름을 두 개 남기면 사용자 개념이 다시 갈라진다. 단, 한 언어에서 외부 패키지
호환이 반드시 필요하다고 판단되면 alias는 별도 결정으로 분리하고 guide에는 새 이름만 노출한다.

### 2.4 RemoteAddress 처리 원칙

`SpotRemoteAddress`는 spot 전송 대상 handle이 아니다. router channel id와 target node 같은
라우팅 구현 정보를 함께 담고 있어서 일반 사용자 메시징 개념으로 보이면 안 된다.

적용 기준:

- 일반 application guide와 sample에서는 `SpotRemoteAddress`를 쓰지 않는다.
- location store 기반 기본 resolver가 있으면 sample/e2e의 custom `SpotRemoteAddressResolver`
  사용을 `SpotRef` 기반 흐름으로 바꾼다.
- framework 내부에서 route bridge wiring에 필요하면 internal 타입으로 남긴다.
- 외부 routing extension으로 public 유지가 필요하면 `advanced routing extension` 문서로 분리하고,
  spot 메시징의 표준 입력은 계속 `SpotRef`로 둔다.

## 3. 사용자 API 모양

아래는 언어 중립 의미다. 실제 이름은 언어별 casing을 따른다.

```csharp
// actor 대상 메시징
actorClient.SendToActor(actorRef, message);
actorClient.RequestToActor(actorRef, request);

// spot 대상 메시징
spotOutbound.SendToSpot(spotRef, message);
spotOutbound.RequestToSpot(spotRef, request);

// 필요하면 한 번 resolve한 뒤 보관한다.
var spotRef = await spotRefs.ResolveSpotRefAsync(spotRid, cancellationToken);
await spotOutbound.SendToSpot(spotRef, message).SendAsync(cancellationToken);
```

전송 API는 내부에서 location store를 조회하지 않는다. 대량 메시지 전송에서는 사용자가 한 번 얻은
`ActorRef` 또는 `SpotRef`를 보관하고 재사용한다. stale 실패가 나면 다시 resolve한다.

금지되는 API 모양:

```csharp
// 제거 대상: 전송 API가 actor id를 받아 내부 resolve까지 수행한다.
actorClient.SendToActor(actorId, message);
actorClient.RequestToActor(actorId, request);

// 제거 대상: 전송 API가 spot id만 받아 내부 resolve까지 수행한다.
spotOutbound.SendToSpot(spotRid, message);
spotOutbound.RequestToSpot(spotRid, request);

// 제거 대상: node rid와 spot rid를 낱개로 받아 순서 실수를 막지 못한다.
spotOutbound.SendToSpot(nodeRid, spotRid, message);
spotOutbound.RequestToSpot(nodeRid, spotRid, request);
```

## 4. 공통 변경 순서

1. 공통 문서에서 `SpotAddress` 기반 설명을 `SpotRef` 기반 설명으로 바꾼다.
2. 각 언어의 public contract 타입을 추가/rename한다.
3. resolver 이름과 반환 타입을 `SpotRef` 기준으로 바꾼다.
4. actor id만 받는 actor 메시징 API를 제거하고, `ActorRef`를 받는 API를 추가한다.
5. spot id 또는 node id + spot id를 받는 spot 메시징 API를 제거하고, `SpotRef`를 받는 API를
   추가한다.
6. spot/route/channel/actor outbound API 인자명을 `address` 또는 `spotRid`에서 `spotRef`로
   바꾼다.
7. runtime 내부 타입은 public 타입 rename을 따라가되, route bridge 내부 구현 세부는 public으로
   노출하지 않는다.
8. sample/e2e에서 `SpotAddress` 생성과 import를 모두 `SpotRef`로 바꾸고, id-only messaging
   호출을 ref 기반 호출로 바꾼다.
9. contract test, unit test, e2e, sample regression을 통과시킨다.
10. 마지막에 `rg`로 old public 이름과 id-only messaging API가 남지 않았는지 확인한다.

## 5. 언어별 적용 계획

언어별 작업자는 아래 worker 문서를 기준으로 진행한다.

| 언어 | worker 문서 |
|------|-------------|
| .NET | `framework/doc/plan/framework-ref-target-unification-dotnet-worker-prompt.ko.md` |
| Java | `framework/doc/plan/framework-ref-target-unification-java-worker-prompt.ko.md` |
| Kotlin | `framework/doc/plan/framework-ref-target-unification-kotlin-worker-prompt.ko.md` |
| Node | `framework/doc/plan/framework-ref-target-unification-node-worker-prompt.ko.md` |
| C++ | `framework/doc/plan/framework-ref-target-unification-cpp-worker-prompt.ko.md` |

### 5.1 .NET

Public contract 변경:

- `ZLinkSpotAddress` -> `SpotRef`
- `IZLinkSpotAddressResolver` -> `IZLinkSpotRefResolver`
- `ResolveSpotAddressAsync` -> `ResolveSpotRefAsync`
- `ResolveActorSpotAddressAsync` -> `ResolveActorSpotRefAsync`
- spot/context/channel 전송 API의 인자 타입과 이름을 `SpotRef spotRef`로 변경
- `IZLinkActorClient.SendToActor<T>(string actorId, ...)`와
  `IZLinkActorClient.RequestToActor<T>(string actorId, ...)`는 제거하고 `ActorRef` 인자 API를 추가
- spot/context/channel에서 `RoutingId spotRid` 또는 `RoutingId nodeRid, RoutingId spotRid`를 받는
  메시징 API는 제거하고 `SpotRef` 인자 API만 남김
- `IZLinkSpotRemoteAddressResolver`와 `ZLinkSpotRemoteAddress`는 application 기본 표면에서 제거
  또는 advanced routing extension으로 분리

현재 변경 대상 파일:

```text
framework/languages/dotnet/src/Zlink.Framework.AspNetCore/ZLinkFrameworkServiceRegistrar.cs
framework/languages/dotnet/src/Zlink.Framework.Locations.Redis/ZLinkRedisLocationRowJson.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Actors/IZLinkActorContext.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Actors/IZLinkActorDirectory.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Actors/IZLinkActorManager.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Channels/RouteCalls.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Configuration/Builders.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/Resolvers.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/Rows.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Spots/SpotRoutingContracts.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Spots/ZLinkSpot.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Streams/IZLinkSession.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Streams/IZLinkSessionActor.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorClient.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorCreationCoordinator.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorDirectory.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorEntrySpotJoinCoordinator.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorEntrySpotRouteInternalPacketDispatcher.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorEntrySpotRoutePackets.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorManagerService.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorRuntimeState.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorSessionDestroy.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorSessionLocationOwnership.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorSessionStreamBinding.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkRemoteActorJoinPackets.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkSessionActorBindingTable.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Backend/Contracts/IZLinkBackendObjects.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Backend/Contracts/IZLinkBackendSocketContracts.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Backend/Contracts/IZLinkBackendSpotContracts.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Backend/DotNet/Mappings/ZLinkDotNetBackendMappings.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Backend/DotNet/Wrappers/ZLinkBackendSpotNodeWrapper.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Backend/DotNet/Wrappers/ZLinkBackendStreamSocketWrapper.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Channels/ZLinkRouteClient.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Configuration/Builders/ZLinkFrameworkOptionsBuilder.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Host/ZLinkActorRemoteJoiner.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Host/ZLinkFrameworkActorFacade.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Host/ZLinkFrameworkRuntimeActors.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/IZLinkActorLocationLifecycle.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkLocationAddressResolvers.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkLocationLifecycle.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkLocationRuntimeQueryService.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkLocationSpotRemoteAddressResolver.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkStoreLocationResolvers.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkActorSessionForwarder.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkEntrySpotActivationOutbound.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkEntrySpotActorDispatcher.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotActivationDispatcher.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotActivationOutbound.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotActorFrameReader.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotClientCalls.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotContextSurfaces.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotOutboundEndpoint.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Streams/ZLinkManagedStream.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Streams/ZLinkSessionActor.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Streams/ZLinkSessionActorBindingRegistry.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Streams/ZLinkSessionActorCoordinator.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Streams/ZLinkSessionContext.cs
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Actors/ActorContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Channels/ChannelContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Configuration/BuilderContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Locations/LocationContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Spots/SpotContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Streams/StreamContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/RedisCrossLanguageTests.cs
framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/RedisLocationFixtureTests.cs
framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/RedisLocationStoreTests.cs
framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/TestRows.cs
framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Regression.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Configuration/Registration/Monitoring.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Configuration/Registration/NodesAndServices.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Configuration/Registration/Support.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Contracts/ScaffoldSmokeTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/EntrySpotActorDispatchTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/EnvelopeCodecTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/InMemoryLocationStoreTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/LocationLifecycleTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/LocationResolverTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/LocationRuntimeQueryTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/LocationRuntimeTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/SessionActorCoordinatorTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/UnhandledDispatchPolicyTests.cs
```

### 5.2 Java

Public contract 변경:

- `ZLinkSpotAddress` -> `SpotRef`
- `ZLinkSpotAddressResolver` -> `ZLinkSpotRefResolver`
- `resolveSpotAddressAsync` -> `resolveSpotRefAsync`
- `resolveActorSpotAddressAsync` -> `resolveActorSpotRefAsync`
- `ZLinkRouteClient`, `ZLinkSpotOutbound`, actor/spot runtime의 전송 인자명을 `spotRef`로 변경
- `ZLinkActorClient.sendToActor(String actorId, ...)`와
  `ZLinkActorClient.requestToActor(String actorId, ...)`는 제거하고 `ActorRef` 인자 API를 추가
- `ZLinkSpotOutbound.sendToSpot(RoutingId spotRid, ...)`와
  `ZLinkSpotOutbound.requestToSpot(RoutingId spotRid, ...)`는 제거하고 `SpotRef` 인자 API를 추가
- `ZLinkRouteClient`에서 `ZLinkSpotAddress` 또는 id 쌍을 받던 spot 메시징 API는 `SpotRef` 인자만
  받도록 변경
- `ZLinkSpotRemoteAddressResolver`는 일반 guide와 sample에서 제거하고 내부 또는 advanced extension으로 분류

현재 변경 대상 파일:

```text
framework/languages/java/e2e/SpotService/Shared/src/main/java/systems/zlink/e2e/spotservice/shared/SpotRouteResolver.java
framework/languages/java/e2e/SpotService/feature-map.ko.md
framework/languages/java/e2e/YieldDispatch/Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/BindActorsHandler.java
framework/languages/java/e2e/YieldDispatch/Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/PlayBindActorsHandler.java
framework/languages/java/e2e/YieldDispatch/Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/RemoteSpotYieldSessionHandler.java
framework/languages/java/e2e/YieldDispatch/Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/ScenarioReqHandler.java
framework/languages/java/e2e/YieldDispatch/Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/ShutdownYieldSessionHandlers.java
framework/languages/java/e2e/YieldDispatch/Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/SpotCommandHandler.java
framework/languages/java/samples/java/Bingo/Server/Play/src/main/java/systems/zlink/samples/bingo/server/play/infrastructure/zlink/handlers/EnsurePlayerActorHandler.java
framework/languages/java/samples/java/Bingo/Server/Session/src/main/java/systems/zlink/samples/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CourierGateway/src/main/java/systems/zlink/samples/deliverydispatch/server/couriergateway/handlers/BindCourierHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CourierGateway/src/main/java/systems/zlink/samples/deliverydispatch/server/couriergateway/handlers/OfferDeliveryHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CourierSession/src/main/java/systems/zlink/samples/deliverydispatch/server/couriersession/sessions/CourierSession.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CourierSpotNode/src/main/java/systems/zlink/samples/deliverydispatch/server/courierspotnode/handlers/EnsureCourierActorHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CustomerGateway/src/main/java/systems/zlink/samples/deliverydispatch/server/customergateway/handlers/EnsureCustomerActorHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CustomerGateway/src/main/java/systems/zlink/samples/deliverydispatch/server/customergateway/sessions/handlers/SubscribeDeliverySessionHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/Dispatch/src/main/java/systems/zlink/samples/deliverydispatch/server/dispatch/DispatchWorker.java
framework/languages/java/samples/java/DeliveryDispatch/Shared/src/main/java/systems/zlink/samples/deliverydispatch/shared/contracts/Messages.java
framework/languages/java/samples/java/ShoppingMall/Server/OrderWorkflow/src/main/java/systems/zlink/samples/shoppingmall/server/orderworkflow/OrderWorkflowService.java
framework/languages/java/zlink-framework-core/src/contractTest/java/systems/zlink/framework/locations/LocationStoreContractTest.java
framework/languages/java/zlink-framework-core/src/integrationTest/java/systems/zlink/framework/runtime/ActorManagerTest.java
framework/languages/java/zlink-framework-core/src/integrationTest/java/systems/zlink/framework/runtime/ChannelMessagingTest.java
framework/languages/java/zlink-framework-core/src/integrationTest/java/systems/zlink/framework/runtime/SessionActorsRuntimeIntegrationTest.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorDirectory.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorJoinResult.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorManager.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorRef.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorRefSnapshot.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/channels/ZLinkRouteClient.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/configuration/ZLinkFrameworkOptions.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/locations/ZLinkActorAddressResolver.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/locations/ZLinkActorLocation.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/locations/ZLinkSpotAddress.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/locations/ZLinkSpotAddressResolver.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/ZLinkActorRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/ZLinkSessionActorsRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkChannelRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/configuration/DefaultZLinkFrameworkOptions.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/configuration/ZLinkFrameworkRegistration.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/host/ZLinkFrameworkRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/locations/ZLinkLocationLifecycle.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/locations/ZLinkLocationSpotRemoteAddressResolver.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/locations/ZLinkStoreLocationResolvers.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/spots/ZLinkSpotRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/spots/ZLinkSpotRemoteAddress.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/spots/ZLinkSpotRemoteAddressResolver.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/streams/ZLinkSessionActor.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/streams/ZLinkSessionActors.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/LocationContractTest.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/DefaultZLinkFrameworkOptionsTest.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/NodesAndServicesTest.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/actors/ZLinkActorClientRuntimeTest.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/channels/ZLinkChannelRuntimeTest.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/locations/ZLinkLocationLifecycleTest.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/locations/ZLinkStoreLocationResolversTest.java
```

### 5.3 Kotlin

Kotlin은 Java core public contract 위에 coroutine extension을 얹는다. 따라서 Java 변경을 먼저
적용한 뒤 Kotlin extension과 Kotlin sample/e2e를 맞춘다.

Public contract 변경:

- Kotlin extension의 `ZLinkSpotAddress` import를 `SpotRef`로 변경
- suspending resolver 이름을 `resolveSpotRef`, `resolveActorSpotRef`로 변경
- `sendToSpot`, `requestToSpot` extension 인자를 `spotRef`로 변경
- `sendToActorAwait(actorId, ...)`, `requestToActorAwait(actorId, ...)`는 제거하고
  `ActorRef` 인자 extension을 추가
- spot id만 받는 suspending messaging helper가 있으면 제거하고 `SpotRef` 인자 helper만 남김
- Kotlin sample/e2e에서 `ZLinkSpotAddress(...)` 생성자를 `SpotRef(...)`로 변경

현재 변경 대상 파일:

```text
framework/languages/java/e2e-kotlin/SpotService/Server/MultiNode/src/main/kotlin/systems/zlink/e2e/kotlin/spotservice/multinode/MultiNodeApplication.kt
framework/languages/java/e2e-kotlin/SpotService/Server/Play/src/main/kotlin/systems/zlink/e2e/kotlin/spotservice/play/endpoints/EvidenceHttpServer.kt
framework/languages/java/e2e-kotlin/SpotService/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/spotservice/SpotRouteResolver.kt
framework/languages/java/e2e-kotlin/YieldDispatch/Server/Play/src/main/java/systems/zlink/e2e/kotlin/yielddispatch/PlayBindActorsHandler.java
framework/languages/java/e2e-kotlin/YieldDispatch/Server/Session/src/main/java/systems/zlink/e2e/kotlin/yielddispatch/BindActorsReqHandler.java
framework/languages/java/e2e-kotlin/YieldDispatch/Server/Session/src/main/java/systems/zlink/e2e/kotlin/yielddispatch/RemoteSpotYieldReqRouteHandler.java
framework/languages/java/e2e-kotlin/YieldDispatch/Server/Session/src/main/java/systems/zlink/e2e/kotlin/yielddispatch/SpotMsgRouteHandler.java
framework/languages/java/samples/kotlin/Bingo/Server/Session/src/main/kotlin/systems/zlink/samples/kotlin/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CourierGateway/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/couriergateway/handlers/BindCourierHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CourierGateway/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/couriergateway/handlers/OfferDeliveryHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CourierSession/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/couriersession/sessions/CourierSession.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CustomerGateway/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/customergateway/handlers/EnsureCustomerActorHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/Dispatch/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/dispatch/DispatchWorker.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Shared/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/shared/contracts/Messages.kt
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkFrameworkExtensions.kt
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkLocationExtensions.kt
framework/languages/java/zlink-framework-kotlin/src/test/kotlin/systems/zlink/framework/kotlin/KotlinFrameworkExtensionsContractTest.kt
```

### 5.4 Node

Public contract 변경:

- `ZLinkSpotAddress` interface -> `SpotRef`
- `IZLinkSpotAddressResolver` -> `ZLinkSpotRefResolver`
- `resolveSpotAddress` -> `resolveSpotRef`
- `resolveActorSpotAddress` -> `resolveActorSpotRef`
- spot/channel/runtime 전송 API 인자를 `spotRef`로 변경
- `sendToActor(actorId, ...)`, `requestToActor(actorId, ...)`는 제거하고 `ActorRef` 인자 API를 추가
- `sendToSpot(spotRid, ...)`, `requestToSpot(spotRid, ...)`는 제거하고 `SpotRef` 인자 API를 추가
- `ZLinkSpotRemoteAddressResolver`는 일반 application surface에서 제거하거나 advanced routing
  extension으로 분리

현재 변경 대상 파일:

```text
framework/languages/node/packages/framework/src/contracts/Actors/ZLinkActorContext.ts
framework/languages/node/packages/framework/src/contracts/Actors/ZLinkActorDirectory.ts
framework/languages/node/packages/framework/src/contracts/Actors/ZLinkActorFactory.ts
framework/languages/node/packages/framework/src/contracts/Actors/ZLinkActorManager.ts
framework/languages/node/packages/framework/src/contracts/Common/ActorRef.ts
framework/languages/node/packages/framework/src/contracts/Common/index.ts
framework/languages/node/packages/framework/src/contracts/Locations/Resolvers.ts
framework/languages/node/packages/framework/src/contracts/Locations/Rows.ts
framework/languages/node/packages/framework/src/contracts/Spots/SpotRoutingContracts.ts
framework/languages/node/packages/framework/src/contracts/Streams/IZLinkSessionActor.ts
framework/languages/node/packages/framework/src/runtime/actors/actor-client.ts
framework/languages/node/packages/framework/src/runtime/actors/index.ts
framework/languages/node/packages/framework/src/runtime/backend/contracts/index.ts
framework/languages/node/packages/framework/src/runtime/backend/node/node-backend-adapter-factory.ts
framework/languages/node/packages/framework/src/runtime/channels/index.ts
framework/languages/node/packages/framework/src/runtime/host/index.ts
framework/languages/node/packages/framework/src/runtime/locations/index.ts
framework/languages/node/packages/framework/src/runtime/spots/index.ts
framework/languages/node/packages/framework/src/runtime/streams/index.ts
framework/languages/node/test/contract/actor-manager.test.js
framework/languages/node/test/contract/backend-public-api-only.test.js
framework/languages/node/test/contract/contract-surface.test.js
framework/languages/node/test/contract/entry-spot-dispatch.test.js
framework/languages/node/test/contract/location-redis-store.test.js
framework/languages/node/test/contract/location-runtime.test.js
framework/languages/node/test/contract/stream-runtime.test.js
```

### 5.5 C++

Public contract 변경:

- `spot_address_t` -> `spot_ref_t`
- `spot_address_resolver_t` -> `spot_ref_resolver_t`
- `resolve_spot_address` -> `resolve_spot_ref`
- `resolve_actor_spot_address` -> `resolve_actor_spot_ref`
- 전송 인자는 `const spot_ref_t& spot_ref`로 변경
- `actor_client_t::send_to_actor(std::string actor_id, ...)`와
  `actor_client_t::request_to_actor(std::string actor_id, ...)`는 제거하고 `actor_ref_t` 인자 API를 추가
- `spot_outbound_t::send_to(node_rid_t, spot_rid_t, ...)`와
  `spot_outbound_t::request_to(node_rid_t, spot_rid_t, ...)`는 제거하고 `spot_ref_t` 인자 API를 추가
- `route_client_t::send_to_node(router_channel_id, target_node_rid, target_spot_rid, ...)`와
  `route_client_t::request_to_node(router_channel_id, target_node_rid, target_spot_rid, ...)`는
  제거하고 `spot_ref_t` 인자 API를 추가
- route bridge 내부 packet 타입은 public `spot_ref_t`에서 필요한 필드만 읽도록 유지

현재 변경 대상 파일:

```text
framework/languages/cpp/e2e/DeliveryDispatch/Server/CourierActorNode/main.cpp
framework/languages/cpp/e2e/DeliveryDispatch/Server/CustomerGateway/main.cpp
framework/languages/cpp/e2e/SpotService/Server/Play/Handlers/play_control_handlers.hpp
framework/languages/cpp/e2e/SpotService/Server/Play/Spots/play_actor_model.hpp
framework/languages/cpp/e2e/SpotService/Server/Shared/spot_actor_support.hpp
framework/languages/cpp/e2e/ToActorMessaging/Server/Actor/main.cpp
framework/languages/cpp/e2e/ToActorMessaging/Server/Caller/main.cpp
framework/languages/cpp/e2e/YieldDispatch/Server/Play/Spots/play_spot_types.hpp
framework/languages/cpp/e2e/YieldDispatch/Server/Session/Support/yield_session.hpp
framework/languages/cpp/extensions/framework-locations-redis/include/zlink/locations/redis.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/actors/actor.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/channels/channel.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/locations/resolvers.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/locations/rows.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp
framework/languages/cpp/framework/src/runtime/actors/actor_client.cpp
framework/languages/cpp/framework/src/runtime/actors/actor_gateway_runtime.cpp
framework/languages/cpp/framework/src/runtime/actors/actor_gateway_runtime.hpp
framework/languages/cpp/framework/src/runtime/channels/channel_outbound_exchange.cpp
framework/languages/cpp/framework/src/runtime/channels/route_channel_runtime.cpp
framework/languages/cpp/framework/src/runtime/host/actor_gateway_spot_bridge.cpp
framework/languages/cpp/framework/src/runtime/locations/store_location_resolvers.hpp
framework/languages/cpp/framework/src/runtime/spots/spot_route_internal_dispatcher.cpp
framework/languages/cpp/framework/src/runtime/spots/spot_route_internal_dispatcher.hpp
framework/languages/cpp/framework/src/runtime/spots/spot_route_packets.cpp
framework/languages/cpp/framework/src/runtime/spots/spot_route_packets.hpp
framework/languages/cpp/framework/src/runtime/spots/spot_runtime.cpp
framework/languages/cpp/framework/src/runtime/spots/spot_runtime.hpp
framework/languages/cpp/samples/Bingo/Server/Play/Infrastructure/ZLink/Actors/player_actor.hpp
framework/languages/cpp/samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo_room_spot.hpp
framework/languages/cpp/samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo_entry_spot.hpp
framework/languages/cpp/samples/Bingo/Server/Session/Sessions/Handlers/authenticate_session_handler.hpp
framework/languages/cpp/samples/Bingo/Server/Session/Sessions/bingo_session.hpp
framework/languages/cpp/samples/DeliveryDispatch/Server/CourierActorNode/main.cpp
framework/languages/cpp/samples/DeliveryDispatch/Server/CustomerGateway/main.cpp
framework/languages/cpp/samples/SupportChat/Server/Session/main.cpp
framework/languages/cpp/samples/TicTacToe/Server/Play/Infrastructure/ZLink/Actors/player_actor.hpp
framework/languages/cpp/samples/TicTacToe/Server/Play/Infrastructure/ZLink/Sessions/Handlers/authenticate_play_session_handler.hpp
framework/languages/cpp/samples/TicTacToe/Server/Play/Infrastructure/ZLink/Sessions/play_session.hpp
framework/languages/cpp/samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/tictactoe_entry_spot.hpp
framework/languages/cpp/samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe_game_spot.hpp
framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp
framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_layout_contract.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_actor_gateway.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_channel_messaging.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_locations_redis.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_spot_runtime.cpp
```

## 6. id-only 메시징 API 제거 대상

아래 목록은 현재 public contract와 public에 가까운 framework 표면에서 확인된 제거 대상이다.
actor/spot 생성, 조회, ensure, lifecycle join처럼 id가 본래 입력인 API는 이 목록에 포함하지 않는다.
이 섹션은 "메시지를 보내거나 요청하는 API"만 대상으로 한다.

### 6.1 .NET 제거 대상

```text
framework/languages/dotnet/src/Zlink.Framework/Contracts/Actors/IZLinkActorClient.cs
  SendToActor<TMessage>(string actorId, ...)
  RequestToActor<TRequest>(string actorId, ...)

framework/languages/dotnet/src/Zlink.Framework/Contracts/Spots/ZLinkSpot.cs
  SendToSpot<TMessage>(ZLinkSpotAddress address, ...) -> SpotRef 인자로 교체
  RequestToSpot<TRequest>(ZLinkSpotAddress address, ...) -> SpotRef 인자로 교체

framework/languages/dotnet/src/Zlink.Framework/Contracts/Channels/RouteCalls.cs
  SendToSpot<TMessage>(..., ZLinkSpotAddress address, ...) -> SpotRef 인자로 교체
  RequestToSpot<TRequest>(..., ZLinkSpotAddress address, ...) -> SpotRef 인자로 교체
```

`IZLinkActorContext.JoinSpot(RoutingId spotRid, ...)`는 메시징 API가 아니라 actor join workflow다. 이번
제거 목록에는 넣지 않지만, 문서에서 일반 spot messaging과 섞이지 않게 분리한다.

### 6.2 Java 제거 대상

```text
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorClient.java
  sendToActor(String actorId, Object message)
  requestToActor(String actorId, Object request)

framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/spots/ZLinkSpotOutbound.java
  sendToSpot(RoutingId spotRid, Object message)
  requestToSpot(RoutingId spotRid, Object request)

framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/channels/ZLinkRouteClient.java
  sendToSpot(..., ZLinkSpotAddress address, ...) -> SpotRef 인자로 교체
  requestToSpot(..., ZLinkSpotAddress address, ...) -> SpotRef 인자로 교체
```

Runtime 구현에서 같이 제거해야 하는 파일:

```text
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/ZLinkActorClientRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/spots/ZLinkSpotRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkChannelRuntime.java
```

### 6.3 Kotlin 제거 대상

```text
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkFrameworkExtensions.kt
  sendToActorAwait(actorId, ...)
  requestToActorAwait(actorId, ...)
  sendToSpot(..., address, ...) -> SpotRef 인자로 교체
  requestToSpot(..., address, ...) -> SpotRef 인자로 교체
```

Kotlin은 Java API를 감싸므로 Java에서 id-only 메시징 API를 제거한 뒤 coroutine extension도 같은
표면만 제공해야 한다.

### 6.4 Node 제거 대상

```text
framework/languages/node/packages/framework/src/contracts/Actors/ZLinkActorClient.ts
  sendToActor(actorId: string, ...)
  requestToActor(actorId: string, ...)

framework/languages/node/packages/framework/src/contracts/Spots/Contracts.ts
  sendToSpot(spotRid: RoutingId, ...)
  requestToSpot(spotRid: RoutingId, ...)
```

Runtime 구현에서 같이 제거해야 하는 파일:

```text
framework/languages/node/packages/framework/src/runtime/actors/actor-client.ts
framework/languages/node/packages/framework/src/runtime/actors/index.ts
framework/languages/node/packages/framework/src/runtime/spots/index.ts
framework/languages/node/packages/framework/src/runtime/channels/index.ts
```

### 6.5 C++ 제거 대상

```text
framework/languages/cpp/framework/include/zlink/framework/contracts/actors/actor.hpp
  actor_client_t::send_to_actor(std::string actor_id, ...)
  actor_client_t::request_to_actor(std::string actor_id, ...)
  actor_client_t::send_to_actor_erased(std::string actor_id, ...)
  actor_client_t::request_to_actor_erased(std::string actor_id, ...)

framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp
  request_to(node_rid_t node_rid, spot_rid_t spot_rid, ...)
  send_to(node_rid_t node_rid, spot_rid_t spot_rid, ...)
```

Runtime 구현에서 같이 제거해야 하는 파일:

```text
framework/languages/cpp/framework/src/runtime/actors/actor_client.cpp
framework/languages/cpp/framework/src/runtime/actors/actor_gateway_runtime.cpp
framework/languages/cpp/framework/src/runtime/spots/spot_runtime.cpp
framework/languages/cpp/framework/src/runtime/spots/spot_route_internal_dispatcher.cpp
framework/languages/cpp/framework/src/runtime/channels/channel_outbound_exchange.cpp
```

## 7. 추가 API 회귀 테스트 계획

기존 테스트를 rename만 하는 것으로 끝내지 않는다. 각 언어는 새 public API가 실제로 표준 경로임을
보장하는 회귀 테스트를 추가한다.

### 7.1 추가해야 할 공통 테스트

| 테스트 | 검증 내용 |
|--------|-----------|
| actor ref messaging contract | `ActorRef`를 얻은 뒤 `SendToActor(ActorRef, ...)`와 `RequestToActor(ActorRef, ...)`가 동작한다 |
| actor id messaging no-surface | public contract에 `SendToActor(actorId, ...)` / `RequestToActor(actorId, ...)`가 없다 |
| spot ref resolver contract | spot id로 `SpotRef`를 resolve하고 반환 필드가 기대한 node/spot을 담는다 |
| spot ref messaging contract | `SendToSpot(SpotRef, ...)`와 `RequestToSpot(SpotRef, ...)`가 동작한다 |
| spot id messaging no-surface | public contract에 `SendToSpot(spotRid, ...)` / `RequestToSpot(spotRid, ...)`가 없다 |
| pair-id spot messaging no-surface | public contract에 `SendToSpot(nodeRid, spotRid, ...)` / `RequestToSpot(nodeRid, spotRid, ...)`가 없다 |
| no hidden resolve | ref 기반 전송 호출 중 location store resolver가 호출되지 않는다 |
| stale ref behavior | stale `SpotRef`에서 기존 stale/fail-fast 실패 분류가 유지된다 |
| docs sample compile | guide/sample 코드가 `ActorRef` / `SpotRef` 기반 호출만 사용한다 |

### 7.2 언어별 테스트 위치

| 언어 | 추가/수정 테스트 |
|------|------------------|
| .NET | `Zlink.Framework.ContractTests/Actors`, `Zlink.Framework.ContractTests/Spots`, `Zlink.Framework.ContractTests/Channels`, `Zlink.Framework.UnitTests/Runtime/ActorClientTests.cs`, `LocationResolverTests.cs`, sample regression |
| Java | `LocationContractTest`, `ZLinkActorClientRuntimeTest`, `ZLinkChannelRuntimeTest`, `ChannelMessagingTest`, `LocationStoreContractTest` |
| Kotlin | `KotlinFrameworkExtensionsContractTest`, Kotlin e2e/sample compile gate |
| Node | `contract-surface.test.js`, `actor-manager.test.js` 또는 actor client contract test, `location-runtime.test.js`, `entry-spot-dispatch.test.js` |
| C++ | `test_cpp_framework_contract_headers.cpp`, `test_cpp_framework_layout_contract.cpp`, `test_cpp_framework_actor_gateway.cpp`, `test_cpp_framework_channel_messaging.cpp`, `test_cpp_framework_spot_runtime.cpp` |

## 8. 문서 변경 대상

정식 spec/guide는 각 언어 worker가 코드와 테스트를 바꾸는 같은 작업 안에서 함께 갱신한다.
문서만 처리하는 별도 worker를 두지 않는다. `framework/doc/plan/**`과 `framework/doc/**/draft/**`
문서는 이번 섹션의 완료 게이트에서 제외한다. 계획 문서와 초안 문서는 별도 추적용으로 남길 수 있지만,
사용자와 구현자가 읽는 정식 문서는 모두 새 개념으로 맞아야 한다.

각 언어 worker는 자기 언어 문서와, 변경한 public contract가 영향을 주는 공통 문서를 같이 수정한다.
공통 문서에 남은 오래된 이름은 다른 worker에게 넘기지 말고 발견한 worker가 함께 고친다.

현재 검색 기준으로 plan/draft를 제외하고 반드시 확인할 문서는 다음과 같다.

```text
framework/doc/contract-inventory/framework-public-contract-inventory.json
framework/doc/framework/bug/cross-node-actor-disconnect-lifecycle.ko.md
framework/doc/framework/common/README.ko.md
framework/doc/framework/common/e2e/README.ko.md
framework/doc/framework/common/e2e/config-1-location-messaging.ko.md
framework/doc/framework/common/e2e/config-2-spot-service.ko.md
framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md
framework/doc/framework/common/e2e/config-9-to-actor-messaging.ko.md
framework/doc/framework/common/perf/README.ko.md
framework/doc/framework/common/sample/README.ko.md
framework/doc/framework/common/sample/bingo/README.ko.md
framework/doc/framework/common/sample/deliverydispatch/README.ko.md
framework/doc/framework/common/sample/supportchat/README.ko.md
framework/doc/framework/common/sample/tictactoe/README.ko.md
framework/doc/framework/common/spec/actor-model.ko.md
framework/doc/framework/common/spec/channel-topology.ko.md
framework/doc/framework/common/spec/framework-api.ko.md
framework/doc/framework/common/spec/interaction-model.ko.md
framework/doc/framework/common/spec/location-runtime.ko.md
framework/doc/framework/common/spec/location-store-redis.ko.md
framework/doc/framework/common/spec/session-actor-dispatch.ko.md
framework/doc/framework/common/spec/spot-address-messaging.ko.md
framework/doc/framework/cpp/guide/03-concepts.ko.md
framework/doc/framework/cpp/guide/16-grpc-alternative.ko.md
framework/doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md
framework/doc/framework/cpp/internals/cpp-framework-policy.ko.md
framework/doc/framework/cpp/internals/cpp-framework-posd-refactoring-log.ko.md
framework/doc/framework/cpp/spec/actor-gateway-session-relay.ko.md
framework/doc/framework/cpp/spec/cpp-framework-interfaces.ko.md
framework/doc/framework/cpp/spec/cpp-spot.ko.md
framework/doc/framework/dotnet/guide/03-concepts.ko.md
framework/doc/framework/dotnet/guide/05-spot.ko.md
framework/doc/framework/dotnet/guide/06-actor-spot.ko.md
framework/doc/framework/dotnet/guide/07-actor-session.ko.md
framework/doc/framework/dotnet/guide/09-location.ko.md
framework/doc/framework/dotnet/guide/11-feature-map.ko.md
framework/doc/framework/dotnet/guide/12-interface-catalog.ko.md
framework/doc/framework/dotnet/guide/13-grpc-alternative.ko.md
framework/doc/framework/dotnet/guide/case-studies/15-case-realtime-game.ko.md
framework/doc/framework/dotnet/guide/case-studies/17-1-case-marketplace-chat.ko.md
framework/doc/framework/dotnet/guide/case-studies/17-2-case-live-commerce-chat.ko.md
framework/doc/framework/dotnet/guide/case-studies/17-3-case-game-chat.ko.md
framework/doc/framework/dotnet/guide/case-studies/17-case-chat-messaging.ko.md
framework/doc/framework/dotnet/guide/samples/bingo-game-sample.ko.md
framework/doc/framework/dotnet/guide/samples/spot-samples.ko.md
framework/doc/framework/dotnet/guide/samples/stream-samples.ko.md
framework/doc/framework/dotnet/guide/samples/supportchat-sample.ko.md
framework/doc/framework/dotnet/guide/samples/tictactoe-game-sample.ko.md
framework/doc/framework/dotnet/internals/backend-dependency-policy.ko.md
framework/doc/framework/dotnet/internals/behavior-matrix.ko.md
framework/doc/framework/dotnet/internals/di-capability-exposure-policy.ko.md
framework/doc/framework/dotnet/internals/lifecycle-and-failure-semantics.ko.md
framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md
framework/doc/framework/dotnet/spec/aspnet-core-actor.ko.md
framework/doc/framework/dotnet/spec/aspnet-core-channel-messaging.ko.md
framework/doc/framework/dotnet/spec/aspnet-core-location.ko.md
framework/doc/framework/dotnet/spec/aspnet-core-spot.ko.md
framework/doc/framework/dotnet/spec/aspnet-core-stream.ko.md
framework/doc/framework/dotnet/spec/handler-interfaces.ko.md
framework/doc/framework/dotnet/spec/session-actor-dispatch.ko.md
framework/doc/framework/dotnet/spec/spot-node.ko.md
framework/doc/framework/java/guide/06-actor-session.ko.md
framework/doc/framework/java/guide/case-studies/15-case-realtime-game.ko.md
framework/doc/framework/java/guide/samples/bingo-game-sample.ko.md
framework/doc/framework/java/internals/di-capability-exposure-policy.ko.md
framework/doc/framework/java/internals/dotnet-to-java-surface-mapping.ko.md
framework/doc/framework/java/internals/regression-test-matrix.ko.md
framework/doc/framework/java/spec/handler-interfaces.ko.md
framework/doc/framework/java/spec/spring-boot-actor-session.ko.md
framework/doc/framework/java/spec/spring-boot-registry.ko.md
framework/doc/framework/java/spec/spring-boot-spot.ko.md
framework/doc/framework/java/spec/spring-boot-stream.ko.md
framework/doc/framework/kotlin/guide/06-actor-session.ko.md
framework/doc/framework/kotlin/guide/case-studies/15-case-realtime-game.ko.md
framework/doc/framework/kotlin/guide/samples/bingo-game-sample.ko.md
framework/doc/framework/node/guide/07-stream.ko.md
framework/doc/framework/node/guide/case-studies/15-case-realtime-game.ko.md
framework/doc/framework/node/internals/behavior-matrix.ko.md
framework/doc/framework/node/internals/di-capability-exposure-policy.ko.md
framework/doc/framework/node/internals/lifecycle-and-failure-semantics.ko.md
framework/doc/framework/node/internals/regression-test-matrix.ko.md
framework/doc/framework/node/spec/handler-interfaces.ko.md
framework/doc/framework/node/spec/nestjs-actor.ko.md
framework/doc/framework/node/spec/nestjs-channel-messaging.ko.md
framework/doc/framework/node/spec/nestjs-overview.ko.md
framework/doc/framework/node/spec/nestjs-spot.ko.md
framework/doc/framework/node/spec/nestjs-stream.ko.md
framework/doc/framework/node/spec/session-actor-dispatch.ko.md
framework/doc/framework/node/spec/spot-node.ko.md
```

문서 갱신 시 `Spot 주소 기반 메시징` 같은 제목도 `SpotRef 기반 메시징`처럼 사용자 개념에 맞게
바꾼다. 단, 내부 row나 store 설명에서 실제 저장된 위치 정보를 말할 때는 "location row"라고 풀어
쓴다.

각 worker는 코드 검색과 함께 아래 문서 검색을 자기 완료 게이트에 포함한다.

```bash
rg -n "SpotAddress|spot address|SpotRemoteAddress|spot remote address|SendToActor\\([^)]*actorId|RequestToActor\\([^)]*actorId|sendToActor\\([^)]*actorId|requestToActor\\([^)]*actorId|SendToSpot\\([^)]*spotRid|RequestToSpot\\([^)]*spotRid|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  framework/doc \
  -S -g '!framework/doc/plan/**' -g '!framework/doc/**/draft/**'
```

위 검색에서 남는 항목은 내부 구현 설명인지, lifecycle id 입력인지, 실제 오래된 메시징 표면인지
분류해야 한다. 사용자-facing 메시징 설명에 남아 있으면 실패다.

## 9. 검증 기준

### 9.1 no-hit gate

구현 후 아래 검색에서 일반 public contract, guide, sample, e2e에 old 이름이 남으면 실패다.

```bash
rg -n "ZLinkSpotAddress|spot_address_t|SpotAddress|spot address|ResolveSpotAddress|resolve_spot_address|resolveSpotAddress" \
  framework/languages framework/doc \
  -S -g '!**/build/**' -g '!**/bin/**' -g '!**/obj/**' -g '!**/node_modules/**'
```

id-only messaging API 제거 확인:

```bash
rg -n "SendToActor\\([^)]*actorId|RequestToActor\\([^)]*actorId|sendToActor\\([^)]*actorId|requestToActor\\([^)]*actorId|send_to_actor\\s*\\([^)]*actor_id|request_to_actor\\s*\\([^)]*actor_id|SendToSpot\\([^)]*spotRid|RequestToSpot\\([^)]*spotRid|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid|send_to\\s*\\([^)]*spot_rid|request_to\\s*\\([^)]*spot_rid" \
  framework/languages framework/doc \
  -S -g '!**/build/**' -g '!**/bin/**' -g '!**/obj/**' -g '!**/node_modules/**'
```

예외로 허용할 수 있는 위치:

- 마이그레이션 계획 문서
- release note 또는 호환성 안내 문서
- internal route bridge 구현에서 "old wire compatibility"를 설명하는 주석

허용 예외는 문서에 명시해야 한다. 조용히 남기지 않는다.

### 9.2 언어별 test gate

| 언어 | 최소 검증 |
|------|-----------|
| .NET | `Zlink.Framework.ContractTests`, `Zlink.Framework.UnitTests`, locations Redis tests, sample regression |
| Java | framework core test, contract test, integration test |
| Kotlin | kotlin extension test, kotlin sample/e2e compile 및 관련 e2e |
| Node | `npm test` 또는 기존 contract test runner, sample regression |
| C++ | framework contract tests, framework unit tests, 관련 e2e/sample build |

각 언어는 최소한 다음 동작을 검증해야 한다.

- `SpotRef` resolver가 spot rid로 ref를 반환한다.
- actor 위치 조회가 actor가 사는 spot의 `SpotRef`를 반환한다.
- `SendToSpot(SpotRef, ...)`와 `RequestToSpot(SpotRef, ...)`가 store 조회 없이 동작한다.
- `SendToActor(ActorRef, ...)`와 `RequestToActor(ActorRef, ...)`가 store 조회 없이 동작한다.
- stale spot ref에서 기존 stale/fail-fast 계약이 유지된다.
- actor messaging API는 `ActorRef`를 받고 spot messaging API는 `SpotRef`를 받는다.
- actor id/spot id만 받는 메시징 API가 public contract에 남아 있지 않다.

### 9.3 cross-language parity gate

모든 언어에서 아래 문장이 그대로 성립해야 한다.

> 메시지를 보낼 대상은 먼저 ref로 얻고, 전송할 때 그 ref를 넘긴다.

언어별 public surface diff에서 다음 이름이 대응되어야 한다.

| 의미 | .NET | Java | Kotlin | Node | C++ |
|------|------|------|--------|------|-----|
| actor ref | `ActorRef` | `ActorRef` | `ActorRef` | `ActorRef` | `actor_ref_t` |
| actor messaging | `SendToActor(ActorRef, ...)` | `sendToActor(ActorRef, ...)` | `sendToActorAwait(ActorRef, ...)` | `sendToActor(ActorRef, ...)` | `send_to_actor(actor_ref_t, ...)` |
| spot ref | `SpotRef` | `SpotRef` | `SpotRef` | `SpotRef` | `spot_ref_t` |
| spot messaging | `SendToSpot(SpotRef, ...)` | `sendToSpot(SpotRef, ...)` | `sendToSpot(..., SpotRef, ...)` | `sendToSpot(SpotRef, ...)` | `send_to(spot_ref_t, ...)` |
| spot ref resolver | `IZLinkSpotRefResolver` | `ZLinkSpotRefResolver` | suspending extension | `ZLinkSpotRefResolver` | `spot_ref_resolver_t` |
| resolve spot ref | `ResolveSpotRefAsync` | `resolveSpotRefAsync` | `resolveSpotRef` | `resolveSpotRef` | `resolve_spot_ref` |
| resolve actor spot ref | `ResolveActorSpotRefAsync` | `resolveActorSpotRefAsync` | `resolveActorSpotRef` | `resolveActorSpotRef` | `resolve_actor_spot_ref` |

## 10. ZLink prefix naming policy

이번 작업부터 framework public type rename은 아래 표를 기준으로 한다. 언어별 취향으로 예외를 만들지
않고, 모든 언어가 같은 분류 규칙을 따른다.

| 타입 분류 | 규칙 | 예 | 이번 작업 지시 |
|-----------|------|----|----------------|
| ZLink 고유 값 개념 | `ZLink`를 붙이지 않는다 | `ActorRef`, `SpotRef`, `RoutingId`, `SpotRid` | `ZLinkActorRef`는 `ActorRef`로, `ZLinkSpotAddress`는 `SpotRef`로 바꾼다 |
| 고유 값의 snapshot/serializable form | 원래 값 이름을 유지하고 필요한 suffix만 붙인다 | `ActorRefSnapshot`, `SpotRefSnapshot` | `ZLinkActorRefSnapshot`은 `ActorRefSnapshot`으로 바꾼다 |
| framework service/manager/store/client | `ZLink`를 붙인다 | `ZLinkActorManager`, `ZLinkSpotManager`, `ZLinkLocationStore`, `ZLinkActorClient` | 기존 `ZLink*Manager`, `ZLink*Store`, `ZLink*Client` 이름은 유지한다 |
| framework runtime/host/options/builder | `ZLink`를 붙인다 | `ZLinkRuntime`, `ZLinkHost`, `ZLinkFrameworkOptions`, `ZLinkHostBuilder` | 이번 ref 작업에서 같이 줄이지 않는다 |
| 일반적인 데이터/실행 문맥 타입 | `ZLink`를 붙인다 | `ZLinkMessage`, `ZLinkSession`, `ZLinkContext`, `ZLinkRoute`, `ZLinkHandler` | 일반명 단독 사용을 금지한다 |
| resolver/interface 이름 | service 역할이면 `ZLink`를 붙이고, 반환 값은 prefix 없는 ref를 쓴다 | `ZLinkSpotRefResolver`, `ZLinkActorDirectory` | resolver 타입명은 `ZLinkSpotRefResolver`, 반환 타입은 `SpotRef` |
| C++ public type | C++ 관례에 맞춰 snake_case + `_t`; `zlink_` prefix는 붙이지 않는다 | `actor_ref_t`, `spot_ref_t`, `zlink::framework::location_store_t` | 값 개념은 `actor_ref_t`/`spot_ref_t`, service는 기존 namespace 안 이름 유지 |

작업자가 새 타입을 만들거나 rename할 때는 먼저 타입이 "값 개념"인지 "framework service/role"인지
분류한다. 값 개념이면 짧게 쓰고, service/role이면 `ZLink`를 붙인다. 같은 언어 안에서 같은 분류의
타입에 prefix 정책이 섞이면 실패로 본다.

이번 작업의 명확한 rename 지시:

| 현재 이름 | 최종 이름 | 적용 언어 |
|-----------|-----------|-----------|
| `ZLinkActorRef` | `ActorRef` | .NET에 이미 짧은 이름이 있으면 유지, Java/Kotlin은 rename, Node는 기존 `ActorRef` 유지 |
| `ZLinkActorRefSnapshot` | `ActorRefSnapshot` | .NET은 기존 `ActorRefSnapshot` 유지, Java/Kotlin은 `ActorRefSnapshot`으로 rename, Node는 `ActorRefSnapshot`, C++은 `actor_ref_snapshot_t` |
| `ZLinkSpotAddress` | `SpotRef` | .NET, Java, Kotlin, Node |
| `spot_address_t` | `spot_ref_t` | C++ |
| `ZLinkSpotAddressResolver` | `ZLinkSpotRefResolver` | Java |
| `IZLinkSpotAddressResolver` | `IZLinkSpotRefResolver` | .NET |
| `spot_address_resolver_t` | `spot_ref_resolver_t` | C++ |

## 11. 작업자용 체크리스트

- [ ] 공통 spec/guide에서 `SpotAddress` 용어를 `SpotRef`로 바꾸고 의미를 다시 설명한다.
- [ ] `ZLink` prefix naming policy 표에 따라 값 개념과 service/role 타입을 분리해 rename한다.
- [ ] .NET public contract와 runtime 호출부를 `SpotRef` 기준으로 바꾼다.
- [ ] Java public contract와 runtime 호출부를 `ActorRef`/`SpotRef` 기준으로 바꾼다.
- [ ] Kotlin extension, sample, e2e를 Java 변경에 맞춘다.
- [ ] Node public contract와 runtime 호출부를 `SpotRef` 기준으로 바꾼다.
- [ ] C++ public contract와 runtime 호출부를 `spot_ref_t` 기준으로 바꾼다.
- [ ] actor id/spot id만 받는 메시징 API를 제거하고 ref 기반 API로 대체한다.
- [ ] `SpotRemoteAddress`가 일반 guide/sample에 남지 않도록 제거하거나 advanced extension으로 분리한다.
- [ ] 모든 언어 contract test에서 `ActorRef`/`SpotRef`가 메시징 입력으로 쓰이는지 확인한다.
- [ ] 추가되는 ref 기반 메시징 API 회귀 테스트를 작성한다.
- [ ] 제거되는 id-only messaging API가 public contract에 없다는 negative contract test를 작성한다.
- [ ] no-hit gate를 실행하고 예외를 문서화한다.
- [ ] 각 언어 worker가 자기 언어 문서와 관련 공통 문서를 함께 갱신한다. 별도 문서 worker로 넘기지 않는다.

## 12. 확정 기준과 예외 처리

1. `SpotRemoteAddressResolver`는 일반 application surface에서 제거한다. 꼭 public에 남겨야 하는
   언어별 사유가 있으면 advanced routing extension으로 분리하고, 일반 guide/sample에는 노출하지
   않는다.
2. old 이름 alias는 제공하지 않는 것이 기본 기준이다. 외부 배포 호환성 때문에 한 릴리스 동안
   alias가 필요하면 worker가 별도 호환성 항목으로 표시하고, guide에는 old 이름을 노출하지 않는다.
