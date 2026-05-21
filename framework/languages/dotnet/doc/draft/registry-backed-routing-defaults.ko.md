<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Draft -- ZLink Framework .NET SPOT Timer Policy](./spot-timer-policy.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [Actor](../spec/aspnet-core-actor.ko.md) | [Session Actor Dispatch](../spec/session-actor-dispatch.ko.md) | [Registry](../spec/aspnet-core-registry.ko.md) | [Behavior Matrix](../internals/behavior-matrix.ko.md) | [Regression Matrix](../internals/regression-test-matrix.ko.md) | [공통 discovery draft](../../../../doc/spec/draft/discovery-owner-bound-routes.ko.md) | [공통 route resolver draft](../../../../doc/spec/draft/framework-route-resolvers.ko.md)

# Draft -- ZLink Framework .NET Registry-Backed Routing Defaults

> 이 문서는 **구현 전 초안에서 출발한 설계 정리 문서**다.
> 아직 공개 계약[^public-contract]이 아니며, `.NET` framework 가 Registry 기반
> discovery[^discovery] 를 사용할 때 actor route 와 Spot route 의 기본 구현을
> 어디까지 제공할지 정리한다.

## 1. 목적

Bingo 와 TicTacToe session gateway 샘플은 Registry 를 사용하는 구조를 보여 주는
예제다. 샘플이 별도 파일 저장소나 임시 metadata 저장소를 직접 만들면 사용자는
"Registry 를 쓰려면 framework 밖에서 route 저장소를 직접 만들어야 한다"라고 오해하기
쉽다.

이 문서의 목표는 다음 세 가지다.

1. `UseDiscovery(...)` 와 Registry 기반 route resolver 의 관계를 분명하게 정한다.
2. application 이 직접 관리해야 하는 domain 상태와 framework 가 제공해야 하는 routing
   상태를 나눈다.
3. session 과 actor 사이의 현재 연결 상태는 Registry 에서 검색하지 않는다는 결정을
   문서와 샘플에 같은 의미로 반영한다.

## 2. 핵심 결정

### 2.1 Registry 는 일반 key-value 저장소가 아니다

core Registry 의 route 저장소는 임의 key-value 저장소가 아니다. Registry 는
service/provider row 와 owner-bound route row 를 보관하고, owner 가 사라지거나
registration generation 이 바뀌면 그 owner 가 claim 한 route 도 함께 정리한다.

따라서 `.NET` framework 기본 구현은 Registry 를 임시 파일 저장소처럼 흉내 내지 않는다.
framework 가 의존할 수 있는 것은 binding public API 로 노출된 discovery route
bind, unbind, resolve 기능과 native Spot owner topology 조회다.

### 2.2 기본 구현 범위는 actor route 와 Spot route 로 제한한다

Registry 기반 기본 구현은 다음 capability 만 맡는다.

| 책임 | 기준 | 결정 |
|------|------|------|
| actor id -> play node route 조회 | framework-managed actor route row | 기본 `IZLinkActorPlayRouteResolver` 를 제공한다. actor 생성 경로가 route row 를 publish 하고 resolver 는 그 row 를 `ZLinkActorRoute` 로 변환한다. |
| Spot RID -> owner node route 조회 | Spot owner sync 와 `ResolveSpot(spotRid)` | 기본 `IZLinkSpotRouteResolver` 를 제공한다. RID 조회는 native owner topology 를 사용한다. |
| Spot name -> Spot RID 조회 | framework-managed Spot name directory | string overload 를 위해 Spot name directory 를 framework 가 관리한다. |

반대로 actor 가 현재 연결된 client session 으로 메시지를 보내는 경로는 Registry 기본
구현 범위가 아니다. session 이 actor 에 bind 될 때 session router RID 와 binding token 을
actor runtime state 에 저장하고, actor 의 `Context.SessionProxy` 는 그 값을 사용한다.
보내는 시점에 actor id 로 session 위치를 다시 찾지 않는다.

이 결정은 의도적인 제한이다. actor 와 session 의 현재 연결은 이미 runtime 이 알고 있는
상태이며, 외부 discovery 에서 다시 검색하면 stale 결과와 중복 소유권 문제가 생긴다.

### 2.3 명시 API 를 둔다

`UseDiscovery(...)` 만으로 모든 route resolver 가 암묵적으로 켜지지 않는다.
`UseDiscovery(...)` 는 Registry bootstrap, service list 수신, channel/Spot peer 자동
연결을 위한 설정이다. actor route resolver 와 Spot route resolver 는 application 이
명시적으로 선택해야 한다.

초안 API 는 capability 를 분리한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.UseDiscovery(discovery => discovery.Add(registryEndpoint));

    options.UseRegistrySpotRoutes("bingo");
});
```

actor route resolver 가 필요한 application 은 `UseRegistryActorRoutes("bingo")` 를
명시적으로 추가한다. session gateway 샘플처럼 actor 준비 응답에서 concrete route snapshot 을
받는 구조라면 actor route resolver 를 켜지 않는다.

### 2.4 custom resolver API 는 확장 지점으로 남긴다

다음 API 는 Registry 기본 구현을 쓰지 않는 고급 사용자를 위한 확장 지점이다.

- `AddActorPlayRouteResolver<TResolver>()`
- `AddSpotRouteResolver<TResolver>()`

기본 구현과 custom resolver 를 같은 capability 에 동시에 등록하면 startup validation
오류다. `TryAdd...` 처럼 조용히 한쪽을 무시하면 실제로 어떤 경로가 쓰이는지 알기 어렵다.

## 3. session 연결 상태 규칙

session attach 와 actor push 는 route resolver 문제로 보지 않는다.

session 이 actor handle 을 bind 할 때 이미 actor route snapshot 을 갖고 있어야 한다.
relay 는 이 snapshot 을 사용해서 actor 로 전달한다. actor 가 현재 client session 으로
메시지를 보낼 때는 actor runtime state 에 붙은 session router RID 를 사용한다.

정리하면 역할은 다음과 같이 나뉜다.

| 경로 | 사용하는 route |
|------|----------------|
| session -> attached actor relay | attach 시점에 저장한 actor route snapshot |
| actor -> current client session push | actor runtime state 에 저장된 session router RID |
| backend service -> actor messaging | `IZLinkActorPlayRouteResolver` |
| actor -> Spot name/RID 호출 | `IZLinkSpotRouteResolver` |

따라서 session 위치를 찾기 위한 별도 Registry capability 는 두지 않는다. 이 기능은
설정으로 켜는 것이 아니라 session bind/unbind 흐름의 runtime 상태로 처리한다.

## 4. 값 형식과 오류 의미

Registry route value 는 versioned framework payload 로 둔다. Registry 는 byte value 를
보관할 뿐이고 application 은 이 payload 를 직접 읽거나 쓰지 않는다.

초기 payload 는 다음 정보를 담는다.

| route kind | payload |
|------------|---------|
| actor route | format version, namespace, actor id, target node RID |
| Spot name route | format version, namespace, spot name, spot RID |

format version 이 맞지 않거나 payload decode 에 실패하면 resolver 는 해당 row 를 사용하지
않고 route not found 로 처리한다. 잘못된 payload 를 application 예외로 그대로 노출하지
않는다.

오류 의미는 다음과 같이 둔다.

| 상황 | framework 오류 |
|------|----------------|
| actor route 를 찾지 못함 | `ZLinkFrameworkErrorKind.ActorRouteNotFound` |
| Spot name directory 에서 Spot RID 를 찾지 못함 | `ZLinkFrameworkErrorKind.SpotRouteNotFound` |
| Spot owner RID 를 찾지 못함 | `ZLinkFrameworkErrorKind.SpotRouteNotFound` |
| route payload decode 실패 | route not found 로 처리하고 runtime event 또는 debug log 로 남긴다 |
| Registry/discovery transport 오류 | 원인을 보존하는 framework 예외로 감싼다 |

호출자가 재시도 정책을 선택할 수 있도록 not found 와 transport 오류를 구분한다. 기본
resolver 는 무한 재시도를 하지 않는다.

## 5. 샘플 반영 계획

Bingo 와 TicTacToe session gateway 샘플은 Registry 기본 구현을 보여 주되, session 위치
검색 기능이 있는 것처럼 보이면 안 된다.

샘플 설정은 다음 형태를 기본으로 쓴다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
    options.UseRegistrySpotRoutes("bingo");
});
```

Play 서버처럼 SpotNode 를 소유하는 host 는 기존처럼 `AddSpotMesh(...).UseDiscovery(...)`
도 설정한다. Registry route 기본 API 는 route publish/resolve 기본 구현을 켜는 것이지,
Spot mesh discovery 설정을 대체하지 않는다.

session gateway 샘플은 actor route resolver 를 켜지 않는다. Play 서버의 인증 또는 actor
준비 handler 는 concrete route snapshot 을 응답에 싣고, session handler 는 그 snapshot 을
actor handle bind 에 넘긴다. 인증 뒤 game packet relay 는 session state 에 붙은 actor ref
를 재사용한다.

actor 가 client session 으로 push 할 때는 actor instance 의 `Context.SessionProxy` 를
사용한다. 샘플에 actor id 만 받아 session 위치를 찾아 주는 별도 client abstraction 을
두지 않는다.

## 6. 회귀 테스트

현재 구현이 지켜야 하는 회귀 기준은 다음과 같다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ClientServerTests.DiscoveryClient_Request_And_Send_Work_Across_Hosts` | `EnableClient()` 와 `UseDiscovery(...)` 조합이 manual endpoint 없이 request/send 를 수행한다. |
| `RouteChannelTests.RouteRequest_WorksAcrossDiscoveryAttachedRouters` | route mesh channel 이 Registry discovery 로 peer 를 찾고 routed request 를 처리한다. |
| `NodesAndServicesTests.AddZLinkFramework_Registers_SessionProxy_Factory` | session proxy factory 는 actor context 에서 사용할 수 있도록 등록된다. |
| `NodesAndServicesTests.AddZLinkFramework_Allows_SpotRouteResolver_Without_SpotNode` | Spot route resolver 는 SpotNode 가 없는 route 제공 서버에서도 등록할 수 있다. |
| `RegistryRoutesTests.RegistryActorRoutes_Registers_Default_Service` | `UseRegistryActorRoutes(...)` 가 custom resolver 없이 기본 `IZLinkActorPlayRouteResolver` 를 등록한다. |
| `RegistryRoutesTests.RegistrySpotRoutes_Registers_Default_Service` | `UseRegistrySpotRoutes(...)` 가 custom resolver 없이 기본 `IZLinkSpotRouteResolver` 와 Spot name directory 를 등록한다. |
| `RegistryRoutesTests.RegistryActorRoutes_Require_Discovery` | `UseDiscovery(...)` 없이 Registry actor route resolver 를 켜면 startup validation 오류가 난다. |
| `RegistryRoutesTests.RegistrySpotRoutes_Require_SpotDiscovery` | `UseSpotDiscovery(...)` 없이 Registry Spot route resolver 를 켜면 startup validation 오류가 난다. |
| `RegistryRoutesTests.RegistryRouteResolvers_Reject_Custom_Duplicate` | 기본 구현과 custom resolver 를 함께 등록하면 startup validation 오류가 난다. |
| `RegistryRoutesTests.RegistryRouteResolvers_Require_Explicit_RouterChannel_When_Ambiguous` | route channel 이 둘 이상이면 `RouterChannelId` 를 명시하지 않은 Registry resolver 설정이 startup validation 오류가 된다. |
| `SessionProxyAndHeaderTests.SessionProxy_Uses_Multipart_Routed_Client_Push` | actor -> client session push 가 actor state 의 session route 를 사용해 routed multipart packet 을 보낸다. |
| `ActorSessionStateTests.ActorSessionState_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream` | 이전 stream 의 늦은 disconnect 가 현재 actor-session 연결을 끊지 않는다. |
| `RegistryRoutesTests.RegistrySpotRoutes_Resolves_Created_Spot_By_Name` | `IZLinkSpotManager.CreateAsync(string)` 으로 만든 Spot 을 string overload 로 찾고 제거 후 not found 를 반환한다. |
| `RegistryRoutesTests.RegistrySpotRoutes_Resolves_Created_Spot_By_Rid` | 생성된 Spot 의 RID 를 기본 resolver 로 찾는다. |
| `RegressionTests.Bingo_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | Bingo 샘플에 sample-only registry metadata store, play/Spot route store, route publisher 가 없고 Registry 기본 API 를 사용한다. |
| `RegressionTests.TicTacToe_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | TicTacToe session gateway 샘플도 sample-only registry metadata store 없이 Registry 기본 API 를 사용한다. |

추가로 샘플 회귀 테스트는 session 위치 검색용 Registry 설정이나 actor id 기반 session
client abstraction 이 다시 들어오지 못하게 검사한다.

## 7. 문서 반영 계획

구현이 끝난 내용은 이 draft 에만 남기지 않고 아래 문서에 나누어 반영한다.

| 문서 | 반영 내용 |
|------|-----------|
| `framework/languages/dotnet/doc/spec/aspnet-core-actor.ko.md` | actor route resolver 의 기본 Registry 구현과 custom resolver 의 위치 |
| `framework/languages/dotnet/doc/spec/session-actor-dispatch.ko.md` | session attach 시점의 route snapshot 과 actor state 기반 session proxy 규칙 |
| `framework/languages/dotnet/doc/spec/aspnet-core-spot.ko.md` | Spot route resolver 기본 구현, RID 기반 조회, Registry 기반 Spot name directory |
| `framework/languages/dotnet/doc/spec/aspnet-core-registry.ko.md` | `.NET` Registry 사용 시 framework 가 native owner-bound route/topology 를 사용하는 범위 |
| `framework/languages/dotnet/doc/internals/behavior-matrix.ko.md` | Registry 기본 resolver 와 custom resolver 중복 등록 규칙 |
| `framework/languages/dotnet/doc/internals/regression-test-matrix.ko.md` | §6 의 회귀 테스트 항목과 실제 테스트 이름 |
| `framework/languages/dotnet/doc/guide/samples/bingo-game-sample.ko.md` | Bingo 샘플이 기본 Registry 구현과 actor context session proxy 를 사용하는 흐름 |
| `framework/languages/dotnet/doc/guide/samples/tictactoe-game-sample.ko.md` | TicTacToe session gateway 샘플이 같은 구조를 사용하는 흐름 |
| `framework/languages/dotnet/doc/README.ko.md` | draft 링크 설명과 구현 후 정식 문서 링크 정리 |

공통 문서에도 반영이 필요하다. `.NET` 문서만 바꾸면 Registry 기반 기본 구현이 언어별
임시 편의 기능처럼 보이기 때문이다.

| 공통 문서 | 반영 내용 |
|-----------|-----------|
| `doc/spec/draft/framework-route-resolvers.ko.md` | framework adapter 가 기본 route resolver 를 제공해야 하는 이유와 custom resolver 의 역할 |
| `doc/spec/draft/discovery-owner-bound-routes.ko.md` | Spot name route 를 owner-bound route 로 추가할 때 필요한 route kind, value, unbind 의미 |
| `doc/spec/core/service/discovery.ko.md` | `ResolveSpot`, Spot owner sync, discovery route bind/resolve 중 framework 기본 구현이 의존하는 범위 |
| `doc/spec/core/service/registry.ko.md` | Registry route row 가 일반 key-value 가 아니라 owner-bound projection 이라는 공개 계약 범위 |
| `doc/internals/registry-internals.ko.md` | 구현 변경이 생기면 route row, owner cleanup, materialized winner 설명이 실제 코드와 맞는지 재검토 |
| `doc/internals/discovery-internals.ko.md` | 구현 변경이 생기면 sync flag, resolve actor/spot, route snapshot 설명이 실제 코드와 맞는지 재검토 |

정식 spec 에는 이 draft 의 문제 제기 문장을 그대로 옮기지 않는다. 정식 문서는 공개 계약과
사용 규칙만 담고, 샘플 전환 이유는 이 draft 에 남긴다.

## 8. 구현 상태

이번 변경은 아래 범위까지 반영한다.

1. session 위치 검색용 Registry 설정과 저장소 extension point 를 제거한다.
2. actor 가 client session 으로 push 할 때는 actor runtime state 에 저장된 session router
   RID 를 사용한다.
3. `IZLinkSessionProxyFactory` 는 actor context 에서 사용할 수 있도록 framework 서비스로
   등록한다.
4. Bingo 와 TicTacToe 샘플은 actor id 기반 session client 대신 actor instance 의
   `Context.SessionProxy` 를 사용한다.
5. 회귀 테스트는 제거된 Registry session 설정이 샘플에 다시 들어오지 않는지 확인한다.

이 규칙을 만족하지 못하면 샘플에 임시 저장소를 유지하는 방식으로 우회하지 않는다. 필요한
기능이 framework 또는 binding 에 없으면 기본 구현을 먼저 추가한다.

[^public-contract]: 공개 계약은 사용자가 의존해도 되는 함수, 타입, 예외, 동작 규칙을 뜻한다.
[^discovery]: discovery 는 Registry 같은 외부 디렉토리에서 peer 주소와 route 정보를 받아 자동 연결과 route 조회를 수행하는 메커니즘이다.
