# .NET Worker Prompt: ActorRef / SpotRef 전송 대상 통일

기준 문서: `framework/doc/plan/framework-ref-target-unification-plan.ko.md`

## 목표

.NET framework public contract에서 메시징 대상 개념을 `ActorRef` / `SpotRef`로 통일한다.
actor id 또는 spot id만 받아서 메시지를 보내는 API는 제거한다. id는 조회 입력이고, ref는 전송
입력이다.

## Naming 규칙

| 분류 | 최종 이름 |
|------|-----------|
| actor 전송 대상 값 | `ActorRef` |
| actor ref snapshot | `ActorRefSnapshot` |
| spot 전송 대상 값 | `SpotRef` |
| spot ref resolver | `IZLinkSpotRefResolver` |
| actor/spot manager, client, store, runtime | 기존 `ZLink*` 유지 |
| message/session/context/options/builder/host | 기존 `ZLink*` 유지 |

`ZLinkSpotRef`를 만들지 않는다. 값 개념은 prefix 없이 둔다.

## 제거 대상

```text
framework/languages/dotnet/src/Zlink.Framework/Contracts/Actors/IZLinkActorClient.cs
  SendToActor<TMessage>(string actorId, ...)
  RequestToActor<TRequest>(string actorId, ...)

framework/languages/dotnet/src/Zlink.Framework/Contracts/Spots/ZLinkSpot.cs
  SendToSpot<TMessage>(ZLinkSpotAddress address, ...)
  RequestToSpot<TRequest>(ZLinkSpotAddress address, ...)

framework/languages/dotnet/src/Zlink.Framework/Contracts/Channels/RouteCalls.cs
  SendToSpot<TMessage>(..., ZLinkSpotAddress address, ...)
  RequestToSpot<TRequest>(..., ZLinkSpotAddress address, ...)
```

`IZLinkActorContext.JoinSpot(RoutingId spotRid, ...)`는 actor join workflow이므로 이번 제거 대상이 아니다.
단, 문서에서는 일반 spot messaging과 분리해서 설명한다.

## 추가/변경 대상

- `ZLinkSpotAddress`를 `SpotRef`로 변경한다.
- `IZLinkSpotAddressResolver`를 `IZLinkSpotRefResolver`로 변경한다.
- `ResolveSpotAddressAsync`를 `ResolveSpotRefAsync`로 변경한다.
- `ResolveActorSpotAddressAsync`를 `ResolveActorSpotRefAsync`로 변경한다.
- actor 메시징 API는 `ActorRef`를 받는다.
- spot 메시징 API는 `SpotRef`를 받는다.
- `ZLinkSpotRemoteAddress` / `IZLinkSpotRemoteAddressResolver`는 일반 application surface에서 제거하거나
  advanced routing extension으로 분리한다. 일반 guide/sample에서는 사용하지 않는다.

## 주요 파일

```text
framework/languages/dotnet/src/Zlink.Framework/Contracts/Actors/IZLinkActorClient.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Channels/RouteCalls.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/Resolvers.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/Rows.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Spots/SpotRoutingContracts.cs
framework/languages/dotnet/src/Zlink.Framework/Contracts/Spots/ZLinkSpot.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkActorClient.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Channels/ZLinkRouteClient.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkLocationAddressResolvers.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkLocationSpotRemoteAddressResolver.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkStoreLocationResolvers.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotClientCalls.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotContextSurfaces.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotOutboundEndpoint.cs
```

## 테스트

추가/수정해야 할 테스트:

```text
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Actors/ActorContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Channels/ChannelContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Locations/LocationContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Spots/SpotContracts.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/ActorClientTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/LocationResolverTests.cs
framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/LocationRuntimeTests.cs
framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Regression.cs
```

필수 검증:

- `SendToActor(ActorRef, ...)` / `RequestToActor(ActorRef, ...)`가 동작한다.
- `SendToSpot(SpotRef, ...)` / `RequestToSpot(SpotRef, ...)`가 동작한다.
- id-only messaging API가 public contract에 없다.
- ref 기반 전송 중 location resolver/store가 호출되지 않는다.
- stale `SpotRef` 실패 분류가 기존 계약과 맞다.

## E2E 회귀 확인

ref 기반 전송으로 바꾼 뒤에도 location runtime 이 같은 rid 를 가진 provider 의 endpoint 교체와 재기동을
정확히 반영하는지 확인한다. `ActorRef` / `SpotRef`는 전송 대상 snapshot 이므로, 오래된 ref 는
정해진 실패로 끝나야 하고 새 ref 를 다시 resolve 하면 새 owner/endpoint 로 전송되어야 한다.

특히 같은 routing id 를 가진 provider 를 다른 endpoint 로 교체했다가 원래 endpoint 로 되돌리는
시나리오에서는 이전 owner 의 location row 가 public topology 에서 사라진 뒤 다음 owner 를 시작해야
한다. 그렇지 않으면 consumer 가 살아 있는 `api-a`로만 계속 전송하면서 복구된 `api-b` 전송 회귀를
숨길 수 있다.

다음 항목을 ref 전환 작업 순서에 포함한다.

- `ResilienceLifecycle`의 same-rid handoff 경로(`RL-A2`, `RL-A4`, `RL-B2`, `RL-C2`)를 확인한다.
- provider shutdown 또는 crash 뒤에는 새 provider 를 시작하기 전에 `topology/wait`로 기존 rid 가
  `Ready` 0개가 되었는지 확인하는 검증 경로를 둔다.
- provider 재기동 뒤에는 `ActorRef` / `SpotRef` 재해석 또는 channel request 가 복구된 provider
  evidence 에 도달하는지 확인한다.
- stale ref 로 전송한 실패와 새 ref 로 다시 전송한 성공을 같은 테스트 또는 E2E 로그에서 분리해
  남긴다.

## 문서 변경 대상

코드와 테스트를 바꾸는 같은 작업 안에서 .NET 문서와 관련 공통 문서를 함께 수정한다. 문서 수정은
별도 worker로 넘기지 않는다.

```text
framework/doc/contract-inventory/framework-public-contract-inventory.json
framework/doc/framework/common
framework/doc/framework/dotnet
```

사용자-facing 문서에는 `ActorRef` / `SpotRef` 기반 전송만 남긴다. `JoinSpot(spotRid, ...)`처럼
lifecycle id 입력이 남아야 하는 경우에는 일반 메시징 API가 아니라는 설명을 붙인다.

## 완료 게이트

```bash
dotnet test framework/languages/dotnet/Zlink.Framework.sln

timeout 420s framework/languages/dotnet/e2e/ResilienceLifecycle/run_e2e.sh RL-A2,RL-A4
timeout 420s framework/languages/dotnet/e2e/ResilienceLifecycle/run_e2e.sh RL-B2
timeout 420s framework/languages/dotnet/e2e/ResilienceLifecycle/run_e2e.sh RL-C2

rg -n "ZLinkSpotAddress|ResolveSpotAddress|ResolveActorSpotAddress|SendToActor\\([^)]*actorId|RequestToActor\\([^)]*actorId|SendToSpot\\([^)]*spotRid|RequestToSpot\\([^)]*spotRid" \
  framework/languages/dotnet \
  -S -g '!**/bin/**' -g '!**/obj/**'

rg -n "SpotAddress|spot address|SpotRemoteAddress|spot remote address|SendToActor\\([^)]*actorId|RequestToActor\\([^)]*actorId|SendToSpot\\([^)]*spotRid|RequestToSpot\\([^)]*spotRid" \
  framework/doc/contract-inventory framework/doc/framework/common framework/doc/framework/dotnet \
  -S -g '!framework/doc/plan/**' -g '!framework/doc/**/draft/**'
```
