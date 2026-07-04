<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: STREAM](08-stream.ko.md) | [다음: Monitoring — runtime 이벤트](10-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

# 9. Location — store 기반 자동 연결

> 정식 계약은 공통 스펙 [location runtime](../../common/spec/location-runtime.ko.md)과
> [Redis extension](../../common/spec/location-store-redis.ko.md)이 다룬다. 이 챕터는
> .NET 표면에서 store 를 등록하고 자동 연결·운영 조회를 실제로 어떻게 쓰는지 사용법
> 중심으로 다룬다.

지금까지 챕터는 역할 등록에 endpoint 를 직접 줬다(`EnableServer("tcp://...")`,
`EnableClient("tcp://...")`). **location store** 를 등록하면 endpoint 를 코드에 적지
않아도 된다. 각 서버는 자기 위치(peer location row)를 store 에 자동 등록하고,
client 쪽은 channel 이름만으로 store 에서 상대를 찾아 연결한다. 서버가 늘고 줄면
연결도 따라간다.

## 1. store 등록

공식 Redis extension(`Zlink.Framework.Locations.Redis` 패키지)의 인스턴스를
`AddLocationStore(...)` 로 등록한다. codec 의 serializer 인스턴스 등록과 같은
형태이며, 전용 등록 함수는 없다.

```csharp
builder.Services.AddZLinkFramework(framework =>
{
    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString("redis-host:6379")
        .SetKeyPrefix("myapp:prod")));

    // 서버 쪽: endpoint 는 bind 를 위해 필요하지만, client 가 이 값을 알 필요는 없다.
    framework.AddClientServerChannel("shop.profile")
        .EnableServer("tcp://0.0.0.0:5555")
        .SetRoutingId(RoutingId.From("profile-a"));
});
```

client 는 endpoint 없이 참여를 선언만 한다.

```csharp
builder.Services.AddZLinkFramework(framework =>
{
    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString("redis-host:6379")
        .SetKeyPrefix("myapp:prod")));

    // endpoint 인자가 없다 — 연결 대상은 store 의 peer row 에서 resolve 된다.
    framework.AddClientServerChannel("shop.profile").EnableClient();
});
```

- `SetKeyPrefix` 는 배포(환경)별 격리 접두사다. 같은 Redis 를 여러 환경이 공유해도
  prefix 가 다르면 서로 보이지 않는다.
- 같은 역할에서 자동 연결과 수동 endpoint 연결을 섞지 않는다. 수동으로 등록한
  연결은 자동 reconcile 이 끊지 않는다.
- 사용자 저장소가 필요하면 통합 계약 `IZLinkLocationStore` 를 구현한 인스턴스를 같은
  지점에 등록한다.

## 2. 동작 방식 요약

- 서버 lifecycle 이 시작될 때 peer location row 와 **owner lease** 를 store 에 쓰고,
  heartbeat 주기로 lease 를 갱신한다.
- client 쪽 reconcile 루프가 peer row 를 조회해 연결을 만들고, row 가 사라지거나
  lease 가 만료되면 연결을 정리한다. 정상 종료는 row 를 즉시 지우고, crash 는 lease
  만료로 전파된다.
- store 가 잠시 죽어도 **기존 연결은 유지**된다(fail-static). 이미 연결된 상대와의
  메시징은 store 와 무관하게 계속 동작한다.

타이밍 옵션은 `ConfigureLocations()` 로 조정한다.

```csharp
var locations = framework.ConfigureLocations();
locations.HeartbeatInterval = TimeSpan.FromSeconds(5);
locations.OwnerLeaseTtl = TimeSpan.FromSeconds(15);
locations.PollingInterval = TimeSpan.FromSeconds(1);
locations.StoreFailureGrace = TimeSpan.FromSeconds(30);
```

## 3. 운영 조회

`IZLinkLocationRuntimeQuery` 를 주입받으면 현재 store 상태와 살아 있는 peer row 를
읽을 수 있다. 관리 endpoint, 헬스 체크, E2E 검증에서 쓴다.

```csharp
app.MapGet("/ops/locations", async (IZLinkLocationRuntimeQuery query) =>
{
    var status = await query.GetStatusAsync();
    var peers = await query.ListPeerLocationsAsync(new ZLinkPeerLocationFilter());
    return Results.Ok(new
    {
        storeHealthy = status.StoreHealthy,
        lastError = status.LastError,
        peers = peers.Select(p => new { rid = p.NodeRid?.ToString(), p.Endpoint })
    });
});
```

- `GetStatusAsync()` — store health, 마지막 오류, owner lease 갱신 상태.
- `ListPeerLocationsAsync(filter)` — 살아 있는(lease 유효) peer row 만 반환한다. 죽은 서버의
  row 는 lease 만료 후 자동으로 제외된다.
- topology projection·service summary 조회와 location 이벤트 관측은
  [10-monitoring](10-monitoring.ko.md) 의 `location-runtime` source 와 함께 쓴다.

## 4. spot / actor 위치 조회

SPOT·actor 메시징이 원격 대상을 찾을 때도 같은 store 를 쓴다. resolver 는 **주소**
(`ZLinkSpotAddress`) 를 반환하고 호출자가 보관한다 — 캐시는 없고, 전송이 실패하면
다시 resolve 한다.

```csharp
public sealed class OrderRouter(IZLinkSpotAddressResolver spots)
{
    public async Task<ZLinkSpotAddress> FindRoomAsync(RoutingId roomRid)
    {
        var address = await spots.ResolveSpotAddressAsync(roomRid)
                      ?? throw new InvalidOperationException($"room '{roomRid}' not found");
        return address; // 호출자가 보관하고, 전송 실패 시 재resolve
    }
}
```

`IZLinkActorAddressResolver.ResolveActorSpotAddressAsync(actorId)` 는 actor
가 위치한 spot 의 주소를 돌려준다. 세부 흐름은 [06-actor-spot](06-actor-spot.ko.md)과
[07-actor-session](07-actor-session.ko.md)를 참고한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: STREAM](08-stream.ko.md) | [다음: Monitoring — runtime 이벤트](10-monitoring.ko.md)
<!-- framework-adapter-nav:bottom:end -->
