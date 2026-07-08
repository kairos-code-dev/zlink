# .NET Worker Prompt: ActorRef / SpotRef 전송 대상 통일

기준 문서: `framework/doc/plan/framework-ref-target-unification-plan.ko.md`

## 목표

.NET framework public contract에서 메시징 대상 개념을 `ActorRef` / `SpotRef`로 통일한다.
actor id 또는 spot id만 받아서 메시지를 보내는 API는 제거한다. id는 조회 입력이고, ref는 전송
입력이다.

## 선행 조건: .NET E2E gap closure

ref 전환 작업을 시작하기 전에 `framework/doc/plan/framework-dotnet-e2e-gap-closure-plan.ko.md`를
먼저 완료한다. 공통 framework E2E 문서의 config-1부터 config-9까지 모든 scenario가
`framework/languages/dotnet/e2e/*/feature-map.ko.md`와 runner evidence에 구현 상태로 남아 있어야
한다. 이 단계는 sample 작업을 포함하지 않는다.

이 선행 작업에서 `partial`, `gap`, `public API gap`으로 남은 항목이 있으면 ref 전환 작업을 시작하지
않는다. 필요한 public contract가 없으면 reflection, internal 접근, raw frame, 테스트 전용 adapter로
우회하지 말고 설계 이슈로 분리한다. 선행 작업의 완료 증거는 `dotnet build`, sample을 제외한 framework
test, 모든 .NET E2E runner pass, 누락 리뷰, POSD/DDD 리뷰다.

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

## 메시지 핸들러 등록 정책 동시 적용

Ref 대상 통일 작업 중 .NET sample이나 E2E의 handler 등록 표면을 고치면
`framework/doc/framework/common/spec/framework-api.ko.md`의 `Handler 등록 정책`도 같은 범위에서
적용한다. handler 타입과 attribute로 알 수 있는 packet 이름, actor 타입, request/send/subscription
종류는 등록 호출부에 반복 인자로 넘기지 않는다.

sample 정리는 아래 기준으로 함께 진행한다.

- `TicTacToe` .NET sample은 manual handler registration을 보여 주는 예시로 남긴다.
- `Bingo`, `DeliveryDispatch`, `ShoppingMall`, `SupportChat`, `GameQuest` .NET sample은 manual
  registration을 제거하고 automatic registration만 사용하도록 정리한다.
- 자동 등록은 test-only scan helper가 아니라 실제 `AddZLinkFramework(...)`/handler scan 표면을
  사용한다.
- README와 guide는 TicTacToe를 수동 등록 예시로, 나머지 sample을 자동 등록 예시로 설명한다.

## connection 복구 책임 경계

.NET framework는 이미 core/binding에 넘긴 connection의 복구를 직접 구현하지 않는다. 연결된
connection의 끊김 감지, reconnect interval, reconnect backoff, monitor 기반 reconnect는 core 또는
binding socket option 책임이다. framework가 해도 되는 일은 location/topology desired set 계산과,
아직 성공적으로 core에 맡기지 못한 target에 대한 initial `connect` 재시도다.

감사 대상:

```text
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkAutoConnectReconciler.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkLocationAutoConnectHost.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/ZLinkAutoConnectLoop.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotActivationDispatcher.cs
framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkEntrySpotActorDispatcher.cs
```

처리 기준:

1. `ZLinkAutoConnectReconciler`가 하는 일을 initial connect retry와 topology handover로만 제한한다.
   `Connect(...)`가 성공적으로 core/binding에 전달된 target을 active로 보는 것은 허용하지만, connect
   실패를 삼킨 뒤 active에 넣는 동작이 있으면 수정한다.
2. disconnected monitor event나 framework 상태만 보고 같은 endpoint에 reconnect timer/backoff를
   시작하는 코드가 있으면 제거한다.
3. endpoint 또는 owner 변경 때문에 기존 target을 끊고 새 target에 연결하는 코드는 topology handover로
   남길 수 있다. 테스트 이름과 주석에는 connection recovery가 아니라 topology handover라고 적는다.
4. `ZLinkSpotActivationDispatcher`와 `ZLinkEntrySpotActorDispatcher`의 fixed retry delay는
   actor/session route readiness 수렴 대기인지 확인한다. 연결 복구를 대신하는 retry라면 제거하고
   core/binding 재현으로 분리한다.
5. public guide/spec에 framework가 established connection을 reconnect한다고 읽히는 문구가 있으면
   core/binding 책임으로 바로잡는다.

완료 보고에는 active target 기록이 connect 실패 뒤에 남지 않는다는 근거, disconnected event 기반
framework reconnect loop가 없다는 검색 결과, fixed retry delay가 route readiness 대기인지 또는 분리한
버그인지를 포함한다.

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

rg -n "reconnect|retry|backoff|disconnect.*connect|connect.*disconnect|Disconnected" \
  framework/languages/dotnet/src/Zlink.Framework/Runtime \
  -S -g '!**/bin/**' -g '!**/obj/**'
```
