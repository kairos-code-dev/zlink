# Framework route resolver 기본 구현 초안

이 문서는 구현 전 설계의 배경을 보존하는 draft 이며 현재 공개 계약은 아니다.
정식 계약은 각 framework adapter 의 spec 문서와 core/binding 공개 API 를 따른다.

## 목적

Framework adapter 는 actor, Spot, session binding 이 어느 runtime node 로 향해야 하는지
알아야 한다. 샘플이 이 주소 매핑을 파일이나 별도 metadata store 로 직접 구현하면
사용자는 Registry 를 일반 key-value 저장소처럼 써도 된다고 오해할 수 있다.

따라서 Registry 를 사용하는 기본 경로는 adapter 가 제공해야 한다. application 은
domain 저장소만 구현하고, route 저장과 조회는 framework 기본 구현 또는 명시적으로
교체한 custom resolver/store 에 맡긴다.

## 현재 결정

기본 구현은 역할 별 API 로 켠다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
    options.UseRegistryActorRoutes("game");
    options.UseRegistrySpotRoutes("game");
    options.UseRegistryActorSessionBindings("game");
});
```

`UseDiscovery(...)` 만으로 route resolver 와 session binding store 를 암묵적으로 켜지는
않는다. discovery 는 Registry bootstrap 과 topology 수신을 뜻하고, 어떤 routing 기본값을
쓸지는 application 설정에 드러나야 한다.

## 책임 분리

| 책임 | 기본 구현 | custom 교체 지점 |
|------|-----------|------------------|
| actor id -> play node route | discovery actor route sync 와 `ResolveActor(actorId)` | `AddActorPlayRouteResolver<T>()`. session 에 이미 attach 된 actor relay 용 fallback 이 아니라, session actor ref 가 없는 backend actor messaging 용 route 조회다 |
| Spot RID -> owner node route | discovery Spot owner sync 와 `ResolveSpot(spotRid)` | `AddSpotRouteResolver<T>()` |
| Spot RID -> Spot RID | owner-bound Spot RID route | `AddSpotRouteResolver<T>()` |
| actor id -> current session route | owner-bound `ActorSession` route | `AddActorSessionBindingStore<T>()` |

custom 구현을 붙이는 API 는 유지한다. 다만 같은 책임에 Registry 기본 구현과 custom 구현을
동시에 등록하면 startup validation 오류다. 조용히 한쪽을 무시하면 어떤 저장소가 실제로
쓰이는지 알기 어려워지기 때문이다.

## owner-bound route 사용

Registry 기본 구현은 Registry 를 raw key-value store 로 다루지 않는다. core/binding 이
제공하는 owner-bound route public API 를 사용한다.

```c
zlink_discovery_bind_route(discovery, kind, key, key_size, value, value_size);
zlink_discovery_unbind_route(discovery, kind, key, key_size);
zlink_discovery_resolve_route(discovery, kind, key, key_size, out_value, out_size);
```

.NET binding 에서는 같은 기능을 `Discovery.BindRoute`, `UnbindRoute`, `ResolveRoute` 로
노출한다. route kind 는 최소한 다음 값을 구분한다.

| route kind | 용도 |
|------------|------|
| `Actor` | native actor route sync 가 사용하는 actor owner route |
| `SpotRid` | framework Spot RID directory |
| `ActorSession` | framework actor-session binding |

route key 와 value 형식은 framework 내부 구현 세부 사항이다. application 은 key 를 직접
만들거나 Registry row 를 직접 읽지 않는다.

## 값 형식

Actor route, Spot RID route, actor-session binding route 는 versioned payload 를 사용한다.
Registry 는 byte value 를 저장할 뿐이고 payload 를 해석하지 않는다.

| route kind | key 의미 | value 의미 |
|------------|----------|------------|
| `Actor` | namespace + actor id | version, namespace, actor id, actor node RID, actor generation |
| `SpotRid` | namespace + spot rid | version, namespace, spot rid, Spot RID |
| `ActorSession` | namespace + actor id | version, namespace, actor id, session router RID, binding token |

payload version 이 맞지 않거나 decode 에 실패하면 framework 는 그 row 를 사용하지 않고
route not found 로 처리한다. 잘못된 Registry payload 를 application 예외 형식으로 노출하지
않는다.

## lifecycle

- actor route sync 와 Spot owner sync 는 discovery 가 socket 또는 SpotNode 에 attach 되기
  전에 켠다.
- session gateway 는 actor attach 시점에 받은 concrete actor route snapshot 을 저장한다.
  session relay 는 message 마다 `ResolveActor(actorId)` 나 actor route resolver 를
  호출하지 않는다.
- Spot RID route 는 `IZLinkSpotManager.CreateAsync(...)` 또는
  `GetOrCreateAsync(...)` 가 Spot 생성을 확정한 뒤 publish 하고, Spot 제거 시 unbind 한다.
- actor-session binding route 는 stream session bind/unbind 흐름에서 publish 한다.
- 같은 process 안의 stale unbind 는 binding token 을 다시 확인해서 막는다.
- 다른 owner generation 에서 온 stale unbind 는 Registry owner-bound route cleanup 규칙이
  막는다.

## validation

Registry 기본 구현은 discovery 가 필요하다. 또한 route 결과를 framework route 계약으로
바꾸려면 route mesh channel id 가 필요하다. host 에 route mesh channel 이 정확히 하나이면
그 값을 추론할 수 있지만, 없거나 둘 이상이면 사용자가 명시해야 한다.

기본 구현과 custom 구현을 함께 등록하는 것도 validation 오류다. 이 규칙은 샘플이 다시
임시 metadata store 를 기본 경로처럼 들고 오지 못하게 하는 장치다.

## 문서 반영

이 draft 의 정식 반영 위치는 다음과 같다.

- framework adapter spec: `UseRegistryActorRoutes(...)`, `UseRegistrySpotRoutes(...)`,
  `UseRegistryActorSessionBindings(...)` 사용 규칙
- discovery spec: route sync, `ResolveActor`, `ResolveSpot`, route bind/resolve public API
- registry spec: owner-bound route row 와 owner cleanup 의미
- internals 문서: materialized route winner, owner generation, route snapshot 흐름

정식 spec 에서는 이 draft 의 문제 제기보다 사용자가 의존해도 되는 계약과 제한 사항을
우선 설명한다.
