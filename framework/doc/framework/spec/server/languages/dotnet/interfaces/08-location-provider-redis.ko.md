# .NET location provider와 Redis 공개 인터페이스

[.NET exact interface 목차](README.ko.md) · [Location record](08-location-maintenance.ko.md) ·
[Authority와 checkpoint](08-authority-checkpoint.ko.md)

## 1. Change stamp

```csharp
public enum ZLinkLocationChangeScopeKind
{
    MeshNode = 1,
    Spot = 2,
    Actor = 3,
    OwnerLease = 4,
    ClientServer = 6,
    FanoutPublisher = 7,
    Authority = 8
}

public readonly record struct ZLinkLocationChangeStampScope(
    ZLinkLocationChangeScopeKind Kind,
    string? MeshName)
{
    public string? ChannelName { get; init; }
}

public interface IZLinkLocationChangeStampStore
{
    ValueTask<ulong> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default);
}
```

Change stamp는 선택 capability다. 등록한 store가 이 interface를 구현하지 않아도 polling으로 같은 상태에
수렴해야 한다. Stamp는 변경이 없을 때 목록 조회를 생략하는 최적화이며 correctness authority가 아니다.
Enum 숫자는 store record와 provider 사이의 wire 값이다. `MeshName`은 MeshNode·Spot·Actor·owner와
authority scope를 한정하고, `ChannelName`은 ClientServer와 fanout publisher scope를 한정한다.

## 2. 공식 Redis package

```csharp
namespace Zlink.Framework.Locations.Redis;

public sealed class ZLinkRedisLocationOptions
{
    public string? ConnectionString { get; set; }
    public StackExchange.Redis.ConfigurationOptions? ConfigurationOptions { get; set; }
    public string KeyPrefix { get; set; } = string.Empty;

    public ZLinkRedisLocationOptions SetConnectionString(string connectionString);
    public ZLinkRedisLocationOptions SetConfiguration(
        StackExchange.Redis.ConfigurationOptions configuration);
    public ZLinkRedisLocationOptions SetKeyPrefix(string keyPrefix);
}

public sealed class ZLinkRedisLocationStore :
    IZLinkLocationStore,
    IZLinkRoutingIdSlotAllocationStore,
    IZLinkLocationChangeStampStore,
    IAsyncDisposable
{
    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options);
    public ZLinkRedisLocationStore(Action<ZLinkRedisLocationOptions> configure);
    public ValueTask DisposeAsync();
}
```

`KeyPrefix`와 `ConnectionString` 또는 `ConfigurationOptions` 가운데 하나는 필수다. 두 연결 설정을 모두
제공하면 `ConfigurationOptions`를 사용한다. Redis client connection은 store 인스턴스가 소유하며 host가
store를 dispose한다. Dispose가 시작된 뒤 새 operation은 `ObjectDisposedException`으로 실패하고, 이미 시작한
operation이 끝난 뒤 connection을 해제한다.

Fanout publisher row는 공통 Redis 계약의 `fanout-publisher` kind, ChannelName+PublisherRid
length-prefix key, `channel` HASH field와 canonical JSON을 사용한다. 다른 descriptor의 row·stamp·조회
API를 재사용하지 않는다.

`ZLinkRedisLocationStore`는 `IZLinkLocationStore`가 상속한 `IZLinkAuthorityStore` capability를 같은 Redis
connection과 key namespace에서 제공한다. Entry·User·Instance Spot은 `(MeshName, SpotRid)` 하나의 authority
row와 object generation을 공유하고 Actor transfer도 opaque authority payload로 저장한다. Redis extension은
Spot kind별 write나 phase별 Claim·Commit·Activate method를 제공하거나 payload를 해석하지 않고 expected Store
version CAS와 Redis `TIME` 기반 lease만 적용한다. Descriptor canonical JSON의
type·state contract set은 UTF-8 byte 순서로 정렬하며 weight·capacity·wave·state 갱신에서도 같은 배열을
보존한다.

## 3. 예제

```csharp
services.AddZLinkFramework(options =>
{
    options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString("redis-host:6379") // 공식 Redis extension의 연결 정보를 설정한다.
        .SetKeyPrefix("zlink:game")));          // 다른 배포와 key namespace를 분리한다.

    options.AddRouteMesh("world")
        .Listen(7300)
        .SetRoutingId(nodeRid); // 자동 discovery와 분산 Spot·Actor가 같은 store를 사용한다.
});
```
