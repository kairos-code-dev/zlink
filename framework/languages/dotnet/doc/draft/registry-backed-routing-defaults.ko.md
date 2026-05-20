<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Draft -- ZLink Framework .NET SPOT Timer Policy](./spot-timer-policy.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [Actor](../spec/aspnet-core-actor.ko.md) | [Session Actor Dispatch](../spec/session-actor-dispatch.ko.md) | [Registry](../spec/aspnet-core-registry.ko.md) | [Behavior Matrix](../internals/behavior-matrix.ko.md) | [Regression Matrix](../internals/regression-test-matrix.ko.md) | [공통 discovery draft](../../../../doc/spec/draft/discovery-owner-bound-routes.ko.md) | [공통 route resolver draft](../../../../doc/spec/draft/framework-route-resolvers.ko.md)

# Draft -- ZLink Framework .NET Registry-Backed Routing Defaults

> 이 문서는 **구현 전 초안**이다.
> 아직 공개 계약[^public-contract]이 아니며, `.NET` framework 가 Registry 기반
> discovery[^discovery] 를 사용할 때 actor route, Spot route, actor-session
> binding[^actor-session-binding] 의 기본 구현을 어떻게 제공할지 정리한다.

## 1. 목적

Bingo 와 TicTacToe session gateway 샘플은 Registry 를 사용하는 구조를 보여 주려는
예제다. 그런데 현재 샘플은 `UseDiscovery(...)` 로 Registry 를 켜면서도 다음
저장소를 샘플 내부에서 직접 구현한다.

- `RegistryActorSessionLocationStore`
- `RegistryPlayRouteStore`
- `IRegistryDiscoveryMetadata`
- `FileRegistryDiscoveryMetadata`

이 구조는 사용자가 그대로 따라 하기 쉽다. 샘플은 가능한 모든 확장 방식을 보여 주는
곳이 아니라, 실제 서비스에서 기본으로 선택해도 되는 모양을 보여 주는 곳이어야 한다.
따라서 Registry 를 쓰는 샘플은 framework 가 제공하는 Registry 기반 기본 구현을
사용해야 한다.

이 초안의 목표는 세 가지다.

1. `UseDiscovery(...)` 와 route/session 기본 구현의 관계를 분명하게 정한다.
2. 사용자가 직접 구현해야 하는 domain 저장소와 framework 가 기본 제공해야 하는
   routing 저장소를 나눈다.
3. 샘플, 정식 spec, 공통 문서, 회귀 테스트에 반영할 항목을 구현 전에 고정한다.

## 2. 현재 문제

현재 `.NET` framework 는 다음 세 인터페이스를 공개 extension point 로 둔다.

```csharp
public interface IZLinkActorPlayRouteResolver
{
    ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRouteResolver
{
    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}

public interface IZLinkActorSessionBindingStore
{
    ValueTask BindSessionAsync(
        ZLinkActorSessionBinding binding,
        CancellationToken cancellationToken);

    ValueTask UnbindSessionAsync(
        ZLinkActorSessionUnbind binding,
        CancellationToken cancellationToken);

    ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
        string actorId,
        CancellationToken cancellationToken);
}
```

extension point 자체는 필요하다. Redis, database, static topology, 테스트 전용
in-memory 구현을 붙일 수 있어야 하기 때문이다. 문제는 Registry 를 기본 경로로 쓰는
샘플까지 이 인터페이스를 직접 구현한다는 점이다.

현재 샘플의 파일 기반 metadata store 는 실제 zlink Registry 가 아니다. 그래서
샘플 코드를 보는 사용자는 "Registry 를 쓰려면 별도의 key-value 저장소를 직접 만들어야
한다"라고 오해할 수 있다.

## 3. 결정

### 3.1 Registry 를 일반 key-value 저장소로 다루지 않는다

core Registry 의 route 저장소는 임의 key-value 저장소가 아니다. Registry 는
service/provider row 와 owner-bound route row 를 보관하고, route row 는
`route identity + owner identity + advertising registry` 관찰값에서 materialized
winner 를 만든다. owner 가 사라지거나 registration generation 이 바뀌면 그 owner 가
claim 한 route 도 함께 정리된다.

따라서 `.NET` framework 기본 구현은 "Registry 에 문자열 key 를 넣고 지운다"는 모양으로
설계하지 않는다. framework 가 의존할 수 있는 것은 다음 세 종류다.

- native discovery 가 이미 제공하는 owner-bound route/topology 조회
- core 내부 route protocol 이 이미 갖고 있지만 public C/binding API 로는 아직 노출하지
  않는 bind/unbind/resolve 기능
- Spot name route, actor-session binding 처럼 새 route kind 가 필요한 경우, route kind 와
  owner identity 계약을 core/binding 에 먼저 추가한 뒤 그 위에 올리는 기본 구현

샘플은 이 차이를 숨기면 안 된다. 사용자가 샘플을 그대로 가져가도 실제 서비스의 기본
경로와 같은 설계를 쓰게 해야 한다.

### 3.2 기본 구현 범위를 capability 별로 나눈다

`.NET` framework 는 Registry 기반 기본 구현을 제공하되, 세 책임을 하나의 저장소로
묶어 설명하지 않는다.

| 책임 | core/discovery 기반 | 결정 |
|------|---------------------|------|
| actor id -> play node route 조회 | actor route sync 와 `ResolveActor(actorId)` | framework 기본 resolver 를 제공한다. publish 쪽은 actor route sync 를 켜고, resolve 쪽은 discovery 조회 결과를 `ZLinkActorRoute` 로 변환한다. |
| Spot RID -> owner node route 조회 | Spot owner sync 와 `ResolveSpot(spotRid)` | framework 기본 resolver 를 제공한다. `spotRid` 조회는 native owner topology 를 사용한다. |
| Spot name -> Spot RID 조회 | 현재 native `ResolveSpot` 은 RID 기준 | `IZLinkSpotRouteResolver` 기본 구현은 string overload 도 반드시 지원해야 하므로, Registry 기반 Spot name directory 를 함께 제공한다. |
| actor id -> 현재 client session route 조회 | 현재 public discovery API 로는 충분하지 않음 | 새 owner-bound actor-session route kind 또는 동등한 public API 를 core/binding 에 먼저 추가해야 한다. 임시 파일 store 를 기본 구현처럼 두지 않는다. |

application 샘플은 이 구현을 직접 작성하지 않는다. 샘플은 "Registry discovery 기반
route resolver 를 쓴다"는 의도를 framework 설정으로 표현해야 한다.

### 3.3 명시 API 를 둔다

`UseDiscovery(...)` 만으로 모든 route/session 저장소를 암묵적으로 켜지는 않는다.
`UseDiscovery(...)` 는 Registry bootstrap, service list 수신, channel/Spot peer 자동
연결을 위한 설정이다. actor route resolver, Spot route resolver, actor-session
binding store 는 application 이 어떤 기본값을 쓰는지 명시해야 한다.

초안 API 는 capability 를 분리한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.UseDiscovery(discovery => discovery.Add(registryEndpoint));

    options.UseRegistryActorRoutes("bingo");
    options.UseRegistrySpotRoutes("bingo");
    options.UseRegistryActorSessionBindings("bingo");

    // 기존 extension point 는 custom 구현을 붙일 때만 사용한다.
    // options.AddActorPlayRouteResolver<MyResolver>();
    // options.AddSpotRouteResolver<MyResolver>();
    // options.AddActorSessionBindingStore<MyStore>();
});
```

구현 후 세 capability 가 모두 준비되면 convenience API 를 둘 수 있다.

```csharp
options.UseRegistryBackedActorRouting("bingo");
```

다만 이 convenience API 는 세 하위 등록을 호출하는 짧은 표기일 뿐이다. actor-session
binding capability 가 아직 없는데도 성공한 것처럼 동작하면 안 된다. 필요한 public API 가
없으면 startup validation 에서 명확히 실패해야 한다.

### 3.4 기존 수동 등록 API 는 custom 구현용으로 남긴다

다음 API 는 제거하지 않는다.

- `AddActorPlayRouteResolver<TResolver>()`
- `AddSpotRouteResolver<TResolver>()`
- `AddActorSessionBindingStore<TStore>()`

이 API 는 기본 Registry 구현을 쓰지 않는 고급 사용자를 위한 확장 지점이다. 샘플의
기본 경로에서는 사용하지 않는다.

중복 등록은 startup validation 오류다. 예를 들어
`UseRegistryActorRoutes(...)` 뒤에 `AddActorPlayRouteResolver<T>()` 를 다시
부르면 host 시작 전에 실패해야 한다.

### 3.5 현재 framework 구조에 필요한 확장 지점을 명시한다

현재 framework runtime 의 `IZLinkBackendDiscovery` abstraction 은
`ConnectRegistry(...)` 와 `MemberPeers()` 만 노출한다. 반면 이 기본 구현에는 native
binding 의 `ActorRouteSyncEnabled`, `SpotOwnerSyncEnabled`, `ResolveActor(...)`,
`ResolveSpot(...)` 이 필요하다.

core 도 주의가 필요하다. 현재 route kind 는 public header 가 아니라
`core/src/runtime/services/discovery/route_limits_internal.hpp` 의 내부 상수로만 존재하고,
공개 C API 는 `zlink_discovery_resolve_actor(...)` 와
`zlink_discovery_resolve_spot(...)` 만 제공한다. 따라서 framework 전용 route 를 추가할 때
내부 상수만 늘리는 것으로 끝내면 안 된다. 정식 구현은 public C 계약, .NET binding 계약,
framework 내부 wrapper 를 같은 순서로 열어야 한다.

구현은 다음 원칙을 따른다.

- framework 는 binding public API 를 직접 감싸는 backend wrapper 메서드를 추가한다.
- `System.Reflection` 으로 binding internal/private 멤버를 호출하지 않는다.
- actor route resolver 는 `ResolveActor(actorId)` 결과의 `Actor.NodeRid` 를 사용한다.
  결과의 actor id 가 요청 actor id 와 다르면 route resolve 실패로 처리한다.
- Spot route resolver 는 `ResolveSpot(spotRid)` 결과의 owner node RID 를 사용한다.
- sync flag 는 discovery 를 SpotNode 또는 channel socket 에 attach 하기 전에 설정한다.

actor route sync 는 native SpotNode actor route 에 붙어 있다. framework actor 가 native
SpotNode actor ref 를 만들지 않는 경로까지 자동으로 지원한다고 문서화하지 않는다. 현재
framework 의 actor 생성 경로처럼 SpotNode 가 있고 `node.CreateActor(actorId)` 로 native
actor ref 가 만들어지는 경우를 기본 지원 범위로 둔다.

### 3.6 namespace 는 route identity 에만 반영한다

Registry 기반 기본 구현의 route key 형식과 route value 형식은 application 계약이
아니다. 사용자는 key 문자열을 직접 만들거나 읽지 않는다.

기본 구현 내부는 논리 namespace 를 받아 route identity 충돌을 막는다.

```csharp
options.UseRegistryActorRoutes("bingo");
```

위 `bingo` 값은 application 전체에서 route identity 를 구분하는 namespace 다. 같은
Registry 를 여러 application 이 공유할 때 서로의 actor id 가 충돌하지 않게 한다.

route 값은 capability 마다 다르다.

| 항목 | 값의 출처 | framework 변환 |
|------|-----------|----------------|
| actor play route | native actor route row | `ResolveActor(actorId)` 결과의 target node RID 를 router channel id 와 합쳐 `ZLinkActorRoute` 로 만든다. |
| Spot route | Spot owner topology row | `ResolveSpot(spotRid)` 결과의 owner node RID 를 router channel id, spot RID 와 합쳐 `ZLinkSpotRoute` 로 만든다. |
| Spot name directory | 새 owner-bound Spot name route row | spot name 으로 Spot RID 를 찾은 뒤 RID 기반 Spot route resolve 를 수행한다. |
| actor-session binding | 아직 없음 | 새 route kind 의 value 가 session router id 와 binding token 을 보존해야 한다. |

`RegistryEndpoint` 같은 별도 query endpoint 는 기본 API 에 넣지 않는다. discovery 는
bootstrap endpoint 에서 pub/uplink endpoint 를 배우므로, 기본 구현도 `UseDiscovery(...)`
의 endpoint 목록을 따른다. 별도 endpoint override 는 실제 구현에서 필요한 근거가 확인될
때만 추가한다.

### 3.7 값 형식은 versioned framework payload 로 둔다

framework 가 새 route kind 를 추가할 때 route value 는 versioned payload 로 인코딩한다.
Registry 는 byte value 를 저장할 뿐이고 application 은 이 payload 를 직접 읽거나 쓰지
않는다.

초기 payload 는 다음 정보를 담는다.

| route kind | payload |
|------------|---------|
| Spot name route | format version, namespace, spot name, spot RID |
| actor-session binding route | format version, namespace, actor id, session router id, session routing id, binding token |

format version 이 맞지 않거나 payload decode 에 실패하면 route resolver/store 는 해당 row 를
사용하지 않고 route not found 로 처리한다. 잘못된 payload 를 application 예외로 그대로
노출하지 않는다. 이 규칙은 Registry 내부 형식 변경이 application 코드로 새어 나가지 않게
하기 위한 것이다.

namespace 는 비어 있으면 안 되고, 앞뒤 공백을 허용하지 않는다. namespace 는 route identity
의 일부이므로 runtime 중 변경하지 않는다.

### 3.8 route resolve 오류 의미를 고정한다

Registry/discovery 는 heartbeat, flooding, owner cleanup 주기에 따라 잠시 오래된 결과나
빈 결과를 줄 수 있다. 기본 구현은 이를 강한 일관성으로 포장하지 않는다.

오류 의미는 다음과 같이 둔다.

| 상황 | framework 오류 |
|------|----------------|
| actor route 를 찾지 못함 | `ZLinkFrameworkErrorKind.ActorRouteNotFound` |
| Spot name directory 에서 Spot RID 를 찾지 못함 | `ZLinkFrameworkErrorKind.SpotRouteNotFound` 를 추가해서 사용한다 |
| Spot owner RID 를 찾지 못함 | `ZLinkFrameworkErrorKind.SpotRouteNotFound` 를 추가해서 사용한다 |
| route payload decode 실패 | route not found 로 처리하고 runtime event 또는 debug log 로 남긴다 |
| Registry/discovery transport 오류 | 원인을 보존하는 framework 예외로 감싼다 |

호출자가 재시도 정책을 선택할 수 있도록 not found 와 transport 오류를 구분한다. 기본
resolver/store 는 무한 재시도를 하지 않는다.

현재 코드 일부는 Spot route 실패를 `ActorRouteNotFound` 로 감싼다. Registry 기반 Spot
route 기본 구현을 추가할 때는 이 오류 종류를 분리해 정식 spec 에 반영한다.

### 3.9 DI lifetime 과 노출 규칙

Registry 기본 resolver/store 는 framework runtime 과 같은 host scope 에서 singleton 으로
등록한다. discovery handle 과 route cache 는 host lifecycle 에 묶이며 request scope 에
묶지 않는다.

DI 노출 규칙은 기존 조건부 노출 정책을 따른다.

- `UseRegistryActorRoutes(...)` 는 `IZLinkActorPlayRouteResolver` 를 등록한다.
- `UseRegistrySpotRoutes(...)` 는 `IZLinkSpotRouteResolver` 를 등록한다.
- `UseRegistryActorSessionBindings(...)` 는 `IZLinkActorSessionBindingStore` 를 등록한다.
- `IZLinkSessionProxyFactory` 와 `IZLinkActorSessionClient` 는 actor-session binding store 와
  route mesh channel 이 있을 때만 노출한다.

custom resolver/store 와 Registry 기본 구현을 함께 등록하면 startup validation 오류다.
`TryAdd...` 로 조용히 무시하지 않는다.

### 3.10 Spot name directory 를 기본 구현에 포함한다

현재 `IZLinkSpotRouteResolver` 는 `ResolveSpotRouteAsync(string, ...)` 과
`ResolveSpotRouteAsync(RoutingId, ...)` 를 모두 요구한다. 따라서 RID overload 만 동작하고
string overload 가 실패하는 Registry 기본 resolver 는 공개하지 않는다. 사용자가
`UseRegistrySpotRoutes(...)` 를 호출하면 두 overload 가 모두 실제로 동작해야 한다.

Spot name directory 는 framework 관리 상태다. native `ResolveSpot(spotRid)` 는 이름을
모르기 때문에, framework 는 Spot 생성 흐름에서 다음 route 를 publish 해야 한다.

| route kind | key | value | owner |
|------------|-----|-------|-------|
| Spot name route | namespace + spot name | Spot RID | Spot owner node registration |

이 route kind 가 core/binding public API 로 준비되기 전에는
`UseRegistrySpotRoutes(...)` 를 정식 API 로 공개하지 않는다. 임시로
`RoutingId.FromString(spotName)` 같은 규칙을 기본 계약으로 삼지 않는다. 현재
`IZLinkSpotManager.CreateAsync(string)` 은 native Spot RID 를 생성하므로, 이름에서 RID 를
추론하는 규칙은 기존 생성 흐름과 맞지 않는다.

Spot name route 는 actor route 와 다른 route kind 를 사용한다. actor route kind 에
문자열 prefix 를 섞어 재사용하지 않는다. route kind 가 분리되어야 actor id 와 spot name 이
같아도 서로의 route 를 덮어쓰지 않고, 문서와 테스트가 충돌 조건을 명확히 검증할 수 있다.

### 3.11 stale unbind guard 는 owner-bound route 의미로 해결한다

actor-session binding 은 reconnect 와 disconnect 순서가 엇갈릴 수 있다.
이전 stream 의 늦은 disconnect 가 새 stream 의 binding 을 지우면 안 된다.

이 문제는 임의 key-value store 의 조건부 삭제로 설명하지 않는다. Registry 기본 구현을
쓰려면 actor-session binding 을 owner-bound route 로 모델링해야 한다. unbind 는 현재
owner/generation 이 claim 한 route 관찰값만 제거해야 하며, 다른 owner 또는 새
generation 이 만든 binding 을 지우면 안 된다.

현재 public discovery API 가 이 actor-session binding route 를 충분히 노출하지 않으면,
framework 안에서 파일 store 나 별도 metadata store 로 우회하지 않는다. 먼저
core/binding public API 를 추가한다.

현재 STREAM node 는 discovery 에 service owner 로 등록되지 않는다. 그러므로
actor-session binding route 의 owner identity 를 stream/session 으로 삼으려면 둘 중
하나를 먼저 구현해야 한다.

- STREAM node 를 discovery owned service 로 등록하고 그 registration id 를 owner identity 로
  사용한다.
- 또는 route bind API 가 framework 에서 명시 owner identity 를 전달할 수 있게 한다.

이 owner identity 없이 session route 를 Registry 에 쓰면 owner cleanup 이 동작하지 않아
끊어진 session 의 binding 이 남을 수 있다. 이 상태는 기본 구현으로 인정하지 않는다.

### 3.12 route publish 는 샘플 코드가 아니라 runtime 흐름에 붙인다

play 서버가 actor 를 만들거나 actor 가 Spot 에 join 할 때 route 상태가 Registry 에
갱신되어야 한다. 이 작업은 샘플 handler 가 직접 `RegistryPlayRoutePublisher` 를 호출하는
모양이 아니어야 한다.

구현 방향은 다음과 같다.

- actor route publish 는 native actor route sync 를 사용한다. publish 쪽 discovery 의
  `ActorRouteSyncEnabled` 를 discovery attach 전에 framework runtime 이 켠다.
- Spot owner publish 는 native Spot owner sync 를 사용한다. owner 쪽 discovery 의
  `SpotOwnerSyncEnabled` 를 discovery attach 전에 framework runtime 이 켠다.
- Spot name route publish 는 `IZLinkSpotManager.CreateAsync(...)` 와
  `GetOrCreateAsync(...)` 성공 이후 framework runtime 이 수행한다.
- actor-session binding publish 는 새 owner-bound session route contract 가 생긴 뒤
  stream/session runtime 에 붙인다.
- 샘플 handler 는 "방 만들기", "게임 시작", "join" 같은 domain 동작만 수행한다.

이렇게 해야 application 코드가 Registry 내부 저장 형식이나 owner cleanup 규칙을 알
필요가 없다.

## 4. API 초안

공개 API 는 `IZLinkFrameworkOptions` 에 두는 방향으로 잡는다.

```csharp
public interface IZLinkFrameworkOptions
{
    void UseRegistryActorRoutes(string namespaceName);

    void UseRegistryActorRoutes(
        string namespaceName,
        Action<IZLinkRegistryActorRoutesOptions> configure);

    void UseRegistrySpotRoutes(string namespaceName);

    void UseRegistrySpotRoutes(
        string namespaceName,
        Action<IZLinkRegistrySpotRoutesOptions> configure);

    void UseRegistryActorSessionBindings(string namespaceName);
}

public interface IZLinkRegistryActorRoutesOptions
{
    string RouterChannelId { get; set; }
}

public interface IZLinkRegistrySpotRoutesOptions
{
    string RouterChannelId { get; set; }
}
```

`RouterChannelId` 는 resolve 결과를 framework route contract 로 변환할 때 필요하다.
기본값은 host 에 route channel 이 정확히 하나 있을 때 그 channel 로 추론한다. route
channel 이 없거나 둘 이상이면 사용자가 명시해야 한다. 조용히 첫 channel 을 고르지
않는다.

`UseRegistrySpotRoutes(...)` 는 Spot owner resolve 뿐 아니라 Spot name directory
등록까지 포함한다. 이 API 를 정식으로 공개하려면 core/binding public API 가 framework
Spot name route 의 bind/unbind/resolve 를 지원해야 한다.

`UseRegistryActorSessionBindings(...)` 는 actor-session binding 을 Registry owner-bound
route 로 저장하는 기본 구현을 켠다. 첫 구현은 route mesh channel 이 정확히 하나인 host 를
대상으로 한다. route row 의 owner 는 discovery 가 붙은 route mesh channel registration 이며,
binding token 을 payload 에 넣어 같은 host 안에서 늦게 도착한 unbind 가 새 binding 을
지우지 못하게 한다.

## 5. 샘플 반영 계획

Bingo 와 TicTacToe session gateway 샘플은 구현 후 다음 방향으로 바꾼다.

### 5.1 제거할 샘플 전용 인프라

다음 파일은 샘플에서 제거한다.

- `Server/Infrastructure/IRegistryDiscoveryMetadata.cs`
- `Server/Infrastructure/FileRegistryDiscoveryMetadata.cs`
- `Server/Infrastructure/InMemoryRegistryDiscoveryMetadata.cs`
- `Server/Infrastructure/ActorSessionLocationStore.cs`
- `Server/Infrastructure/RegistryPlayRouteStore.cs`
- `Server/Infrastructure/RegistryPlayRoutePublisher.cs`
- 샘플 runtime 의 metadata directory 설정

현재 checkout 의 샘플 경로는 다음과 같다.

- `framework/languages/dotnet/samples/Bingo/`
- `framework/languages/dotnet/samples/TicTacToe.SessionGateway/`

두 샘플 모두 위 임시 store 와 `RegistryPlayRoutePublisher` 호출을 제거했다. 샘플에 남길
것은 domain logic 이다. 예를 들어 Bingo 에서는 room, player actor, match flow,
notification 만 남는다.

### 5.2 샘플 설정 모양

샘플 서버는 다음 형태를 기본으로 쓴다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
    options.UseRegistryActorRoutes("bingo");
    options.UseRegistrySpotRoutes("bingo");
    options.UseRegistryActorSessionBindings("bingo");
});
```

Play 서버처럼 SpotNode 를 소유하는 host 는 기존처럼 `AddSpotMesh(...).UseDiscovery(...)`
도 설정한다. Registry route 기본 API 는 route publish/resolve 기본 구현을 켜는 것이지,
Spot mesh discovery 설정을 대체하지 않는다.

remote channel client 는 이미 `EnableClient()` 와 discovery 기반 연결로 맞춘다.
특별한 이유가 없는 한 `UseManualConnections(...)` 를 섞지 않는다.

## 6. 회귀 테스트

현재 구현이 이미 막고 있는 회귀 기준은 다음과 같다. 이 테스트들은 새 기본 구현을
추가할 때도 계속 통과해야 한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ChannelMessagingIntegrationTests.DiscoveryClient_Request_And_Send_Work_Across_Hosts` | `EnableClient()` 와 `UseDiscovery(...)` 조합이 manual endpoint 없이 request/send 를 수행한다. |
| `RouteChannelIntegrationTests.RouteRequest_WorksAcrossDiscoveryAttachedRouters` | route mesh channel 이 Registry discovery 로 peer 를 찾고 routed request 를 처리한다. |
| `RegistrationValidationTests.AddZLinkFramework_DoesNot_Register_SessionProxy_Without_BindingStore` | actor-session binding store 가 없으면 `IZLinkSessionProxyFactory` 와 `IZLinkActorSessionClient` 가 DI 에 노출되지 않는다. |
| `RegistrationValidationTests.AddZLinkFramework_Allows_SpotRouteResolver_Without_SpotNode` | Spot route resolver 는 SpotNode 가 없는 route 제공 서버에서도 등록할 수 있다. |
| `RegistrationValidationTests.RegistryActorRoutes_Registers_Default_Service` | `UseRegistryActorRoutes(...)` 가 custom resolver 없이 기본 `IZLinkActorPlayRouteResolver` 를 등록한다. |
| `RegistrationValidationTests.RegistrySpotRoutes_Registers_Default_Service` | `UseRegistrySpotRoutes(...)` 가 custom resolver 없이 기본 `IZLinkSpotRouteResolver` 와 Spot name directory 를 등록한다. |
| `RegistrationValidationTests.RegistryActorSessionBindings_Registers_Default_Service` | `UseRegistryActorSessionBindings(...)` 가 custom store 없이 기본 `IZLinkActorSessionBindingStore`, session proxy, actor session client 를 등록한다. |
| `RegistrationValidationTests.RegistryRouteResolvers_Require_Discovery` | `UseSpotDiscovery(...)` 없이 Registry route resolver 를 켜면 startup validation 오류가 난다. |
| `RegistrationValidationTests.RegistryRouteResolvers_Reject_Custom_Duplicate` | 기본 구현과 custom resolver 를 함께 등록하면 startup validation 오류가 난다. |
| `RegistrationValidationTests.RegistryRouteResolvers_Require_Explicit_RouterChannel_When_Ambiguous` | route channel 이 둘 이상이면 `RouterChannelId` 를 명시하지 않은 Registry resolver 설정이 startup validation 오류가 된다. |
| `RegistrationValidationTests.RegistryActorSessionBindings_Reject_Custom_Duplicate` | Registry 기본 actor-session binding store 와 custom store 를 함께 등록하면 startup validation 오류가 난다. |
| `RegistrationValidationTests.RegistryActorSessionBindings_Require_Discovery` | `UseDiscovery(...)` 없이 Registry actor-session binding store 를 켜면 startup validation 오류가 난다. |
| `StreamIntegrationTests.SessionProxy_Uses_Multipart_Routed_Client_Push` | actor -> client session push 가 routed multipart packet 을 사용한다. |
| `SpotIntegrationTests.ActorSessionState_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream` | 이전 stream 의 늦은 disconnect 가 현재 actor-session 연결을 끊지 않는다. |
| `SpotIntegrationTests.RegistrySpotRoutes_Resolves_Created_Spot_By_Name` | `IZLinkSpotManager.CreateAsync(string)` 으로 만든 Spot 을 string overload 로 찾고 제거 후 not found 를 반환한다. |
| `SpotIntegrationTests.RegistryActorSessionBindings_Preserve_Reconnected_Binding_On_Stale_Unbind` | Registry actor-session binding 기본 store 가 새 binding 이후 도착한 이전 binding unbind 를 무시한다. |
| `SampleRegressionTests.Bingo_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | Bingo 샘플에 sample-only registry metadata store, play/Spot route store, route publisher 가 없고 Registry 기본 API 를 사용한다. |
| `SampleRegressionTests.TicTacToe_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | TicTacToe session gateway 샘플도 sample-only registry metadata store 없이 Registry 기본 API 를 사용한다. |

## 7. 추가 회귀 테스트 계획

기본 구현을 추가할 때는 다음 테스트를 새로 만든다.

| 예정 테스트 | 확인 기준 |
|-------------|-----------|
| `DiscoveryIntegrationTests.RegistryActorRoutes_Enables_ActorRouteSync` | publish 쪽 discovery 의 `ActorRouteSyncEnabled` 가 켜지고 actor route 가 Registry owner-bound route 로 보인다. |
| `DiscoveryIntegrationTests.RegistrySpotRoutes_Enables_SpotOwnerSync` | owner 쪽 discovery 의 `SpotOwnerSyncEnabled` 가 켜지고 `ResolveSpot(spotRid)` 로 owner node RID 를 찾는다. |
| `DiscoveryIntegrationTests.RegistrySpotRoutes_RequestSend_By_Name` | string overload 로 찾은 Spot route 로 request/send 가 성공한다. |
| `StreamIntegrationTests.RegistryActorRoutes_Relays_Stream_Request_To_Remote_Actor` | session 서버가 native actor route resolve 결과를 통해 play 서버 actor 로 request 를 보낸다. |
| `StreamIntegrationTests.RegistryActorSessionBindings_Routes_Actor_Push_To_Current_Stream` | actor-session binding route kind 로 actor 가 현재 stream 에 push 한다. |
| `SpotIntegrationTests.RegistrySpotRoutes_Resolves_Created_Spot_By_Rid` | 생성된 Spot 의 RID 를 기본 resolver 로 찾고 request/send 가 성공한다. |

추가 테스트들은 단순히 빌드 성공을 보는 것이 아니다. 샘플이 다시
`FileRegistryDiscoveryMetadata` 같은 자체 저장소를 들고 오지 못하게 막는 역할을
한다.

## 8. 문서 반영 계획

구현이 끝나면 이 draft 내용을 아래 문서에 나누어 반영한다.

| 문서 | 반영 내용 |
|------|-----------|
| `framework/languages/dotnet/doc/spec/aspnet-core-actor.ko.md` | actor route resolver 의 기본 Registry 구현과 custom resolver 의 위치 |
| `framework/languages/dotnet/doc/spec/session-actor-dispatch.ko.md` | actor-session binding route kind 가 추가될 때 reconnect/stale unbind 규칙 |
| `framework/languages/dotnet/doc/spec/aspnet-core-spot.ko.md` | Spot route resolver 기본 구현, RID 기반 조회, Registry 기반 Spot name directory |
| `framework/languages/dotnet/doc/spec/aspnet-core-registry.ko.md` | `.NET` Registry 사용 시 framework 가 native owner-bound route/topology 를 사용하는 범위 |
| `framework/languages/dotnet/doc/internals/behavior-matrix.ko.md` | Registry 기본 resolver 와 custom resolver/store 중복 등록 규칙 |
| `framework/languages/dotnet/doc/internals/regression-test-matrix.ko.md` | §6 의 회귀 테스트 항목과 실제 테스트 이름, §7 의 추가 테스트 계획 |
| `framework/languages/dotnet/doc/guide/samples/bingo-game-sample.ko.md` | Bingo 샘플이 기본 Registry 구현을 사용하는 흐름 |
| `framework/languages/dotnet/doc/guide/samples/tictactoe-game-sample.ko.md` | TicTacToe session gateway 샘플이 같은 기본 구현을 사용하는 흐름 |
| `framework/languages/dotnet/doc/README.ko.md` | draft 링크와 구현 후 정식 문서 링크 정리 |

공통 문서에도 반영이 필요하다. `.NET` 문서만 바꾸면 "Registry 기반 기본 구현"이
언어별 임시 편의 기능처럼 보이기 때문이다.

| 공통 문서 | 반영 내용 |
|-----------|-----------|
| `doc/spec/draft/framework-route-resolvers.ko.md` | framework adapter 가 기본 route resolver 를 제공해야 하는 이유와 custom resolver 의 역할 |
| `doc/spec/draft/discovery-owner-bound-routes.ko.md` | Spot name route 와 actor-session binding 을 owner-bound route 로 추가할 때 필요한 route kind, value, unbind 의미 |
| `doc/spec/core/service/discovery.ko.md` | `ResolveActor`, `ResolveSpot`, actor route sync, Spot owner sync 중 framework 기본 구현이 의존하는 범위 |
| `doc/spec/core/service/registry.ko.md` | Registry route row 가 일반 key-value 가 아니라 owner-bound projection 이라는 공개 계약 범위 |
| `doc/internals/registry-internals.ko.md` | 구현 변경이 생기면 route row, owner cleanup, materialized winner 설명이 실제 코드와 맞는지 재검토 |
| `doc/internals/discovery-internals.ko.md` | 구현 변경이 생기면 sync flag, resolve actor/spot, route snapshot 설명이 실제 코드와 맞는지 재검토 |

정식 spec 에는 이 draft 의 문제 제기 문장을 그대로 옮기지 않는다. 정식 문서는
공개 계약과 사용 규칙만 담고, 샘플 실수의 배경은 이 draft 에 남긴다.

## 9. 구현 상태

이번 변경은 아래 범위까지 구현한다.

1. `.NET` framework 의 backend discovery wrapper 는 route bind/unbind/resolve 를
   binding 의 public API 로 호출한다. 리플렉션이나 internal 접근은 사용하지 않는다.
2. core/binding 에 owner-bound route public API 와 `Actor`, `SpotName`,
   `ActorSession` route kind 를 둔다.
3. framework 는 `UseRegistryActorRoutes(...)`, `UseRegistrySpotRoutes(...)`,
   `UseRegistryActorSessionBindings(...)` registration API 와 중복 등록 validation 을
   제공한다.
4. actor route resolver, Spot route resolver, actor-session binding store 기본 구현은
   Registry route API 를 사용한다. Spot route resolver 는 RID overload 와 string overload 를
   모두 처리한다.
5. Bingo 와 TicTacToe session gateway 샘플은 sample-only metadata store 와 manual route
   publisher 없이 registry-backed 기본 구현을 사용한다.
6. 회귀 테스트는 기본 서비스 등록, 중복 등록 거부, discovery/route channel validation,
   Spot name resolve, actor-session stale unbind 방어, 샘플 임시 저장소 제거를 검사한다.

남은 후속 작업은 §8 의 정식 문서 반영 계획에 따라 공통 spec/internals 문서를 더 넓게
정리하는 것이다. 이 초안은 구현 배경과 샘플 전환 이유를 보존하기 위해 유지한다.

## 10. 구현 규칙

구현 중 해석이 갈리지 않도록 아래 규칙을 확정한다.

- `ActorRouteSyncEnabled` 와 `SpotOwnerSyncEnabled` 는 discovery 를 SpotNode 또는 socket 에
  attach 하기 전에 설정한다. 이를 위해 `ZLinkBackendDiscoveryFactory.Create(...)` 또는 그
  호출부가 sync option 을 전달할 수 있어야 한다.
- Spot name route 의 owner identity 는 Spot owner node registration 을 따른다. Spot 이
  명시적으로 제거되면 framework runtime 이 name route 를 unbind 한다. owner node 가 죽으면
  Registry owner cleanup 이 name route 를 정리한다.
- actor-session binding route 의 key 는 `namespace + actor id` 이고 value 는 namespace,
  actor id, session router RID, binding token 을 담는 versioned payload 다. route owner 는
  route mesh channel 의 discovery registration 을 따른다. 같은 process 안에서는 unbind 전에
  현재 payload 의 binding token 을 다시 확인해서 늦은 unbind 가 새 binding 을 지우지 못하게
  한다. 다른 owner 에서 온 늦은 unbind 는 Registry 의 owner generation 규칙으로 막는다.
- 첫 구현에서는 capability 별 API 인 `UseRegistryActorRoutes(...)`,
  `UseRegistrySpotRoutes(...)`, `UseRegistryActorSessionBindings(...)` 만 정식 후보로 둔다.
  `UseRegistryBackedActorRouting(...)` convenience API 는 세 capability 구현과 테스트가 모두
  끝난 뒤 필요성이 다시 확인될 때만 추가한다.
- `UseRegistryActorRoutes(...)` 를 호출한 host 가 actor factory 를 등록하면서 SpotNode 를
  전혀 갖지 않으면 startup validation 오류로 막는다. 그런 host 는 native actor route 를
  publish 할 수 없기 때문이다. actor 를 만들지 않고 resolve 만 수행하는 Session/API host 는
  이 오류 대상이 아니다.

이 규칙을 만족하지 못하면 샘플에 임시 파일 저장소를 유지하는 방식으로 우회하지 않는다.
필요한 기능이 framework 또는 binding 에 없으면 기본 구현을 먼저 추가한다.

[^public-contract]: 공개 계약은 사용자가 의존해도 되는 함수, 타입, 예외, 동작 규칙을 뜻한다.
[^discovery]: discovery 는 Registry 같은 외부 디렉토리에서 peer 주소와 route 정보를 받아 자동 연결과 route 조회를 수행하는 메커니즘이다.
[^actor-session-binding]: actor-session binding 은 특정 actor 가 현재 어떤 client stream session 에 연결되어 있는지를 나타내는 상태다.
