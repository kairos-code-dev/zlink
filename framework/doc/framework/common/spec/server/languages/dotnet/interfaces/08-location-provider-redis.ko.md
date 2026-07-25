# .NET location provider와 Redis 공개 인터페이스

[.NET exact interface 목차](README.ko.md) · [Location record](08-location-maintenance.ko.md) ·
[Authority와 relocation](08-authority-relocation.ko.md)

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
Enum 숫자는 store record와 provider 사이의 wire 값이다. `MeshName`은 MeshNode scope에만 사용하고
`ChannelName`은 ClientServer와 fanout publisher scope를 지정한다. Spot·Actor change는 global `Authority`
scope를 사용하며 `MeshName`과 `ChannelName`은 모두 null이다.

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
    IZLinkLocationChangeStampStore,
    IAsyncDisposable
{
    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options);
    public ZLinkRedisLocationStore(Action<ZLinkRedisLocationOptions> configure);
    public ValueTask DisposeAsync();
}

public sealed class ZLinkRedisRelocationOptions
{
    public string? ConnectionString { get; set; }
    public StackExchange.Redis.ConfigurationOptions? ConfigurationOptions { get; set; }
    public string KeyPrefix { get; set; } = string.Empty;

    public ZLinkRedisRelocationOptions SetConnectionString(string connectionString);
    public ZLinkRedisRelocationOptions SetConfiguration(
        StackExchange.Redis.ConfigurationOptions configuration);
    public ZLinkRedisRelocationOptions SetKeyPrefix(string keyPrefix);
}

public sealed class ZLinkRedisRelocationStore :
    IZLinkRelocationStore,
    IAsyncDisposable
{
    public ZLinkRedisRelocationStore(ZLinkRedisRelocationOptions options);
    public ZLinkRedisRelocationStore(Action<ZLinkRedisRelocationOptions> configure);
    public ValueTask DisposeAsync();
}
```

각 options의 `KeyPrefix`와 `ConnectionString` 또는 `ConfigurationOptions` 가운데 하나는 필수다. 두 연결 설정을 모두
제공하면 `ConfigurationOptions`를 사용한다. Redis client connection은 각 store 인스턴스가 소유하며 host가
store를 dispose한다. Dispose가 시작된 뒤 새 operation은 `ObjectDisposedException`으로 실패하고, 이미 시작한
operation이 끝난 뒤 connection을 해제한다.

두 Store는 같은 Redis deployment 또는 cluster를 서로 다른 key prefix로 사용할 수 있고, 서로 다른 Redis를 사용할
수도 있다. Connection 공유 여부는 구현 세부 사항이며 public correctness 계약이 아니다. 같은 deployment에서 prefix가
겹치면 socket bind 전에 startup configuration error로 실패한다.

Fanout publisher row는 공통 Redis 계약의 `fanout-publisher` kind, ChannelName+PublisherRid
length-prefix key, `channel` HASH field와 canonical JSON을 사용한다. 다른 descriptor의 row·stamp·조회
API를 재사용하지 않는다.

`ZLinkRedisLocationStore`는 `IZLinkLocationStore`가 상속한 `IZLinkAuthorityStore` capability를 같은 Redis
connection과 key namespace에서 제공한다. Entry·User·Instance Spot은 global `SpotId` 하나의 [authority](../../../../01-glossary.ko.md#authority)
row와 object generation을 공유하고 Actor relocation도 opaque authority payload로 저장한다. Redis extension은
Spot kind별 write나 phase별 method를 제공하거나 payload를 해석하지 않고 generic placement
`Reserve`·`Commit`·`Abort`, bounded aggregate operation과 expected StoreVersion CAS를 같은 transaction domain에서
제공한다. [Descriptor](../../../../01-glossary.ko.md#descriptor) canonical JSON의 object capability는 object kind와 stable type의 UTF-8 byte 순서로
정렬한다. Type별 `HasSnapshotAdapter`는 해당 kind의 Snapshot adapter 등록 여부만 저장하며 application state의
format, version이나 contract ID를 저장하지 않는다. Weight·capacity·wave·state를 갱신해도 같은 capability 배열과
adapter flag를 보존한다.

Redis aggregate script는 기존 `PrepareAggregateAsync` request의 `OwnerTransition`으로 mode를 판정한다.
`NewOwner`가 하나라도 있으면 해당 participant의 durable allocation delta만 합산한 non-zero capacity를 예약한다.
모두 `Preserve`이면 exact zero capacity와 모든 empty membership mutation을 요구하고 reservation 없이 authority
payload만 atomic하게 변경한다. 이 mode에서는 owner, `ObjectGeneration`, `AuthorityOwnerGeneration`과 durable
Active allocation을 유지한다. Zero capacity와 `NewOwner`, non-zero capacity와 all-Preserve 조합은 `Conflict`이며
Redis key, counter와 aggregate record의 mutation은 0이다. 이 동작은 새 public method 없이 같은
`IZLinkAuthorityStore` aggregate operation으로 제공한다.
Creation terminal key의 RID segment는 transport `RoutingId`의 exact raw bytes 길이와 그 raw bytes의
lowercase hex를 사용한다. Canonical hex text를 UTF-8로 다시 encode하지 않는다. Raw bytes가 `node-a`이면
segment는 `6:6e6f64652d61`이다.

`ZLinkRedisRelocationStore`는 immutable application state, accepted journal, participant payload와 recovery root만
저장한다. Location authority나 participant membership을 변경하지 않으며 `ZLinkRedisLocationStore`가
`IZLinkRelocationStore`를 함께 구현하지 않는다. Location participant set이 authority이고 Relocation manifest는 payload
lookup용 projection이다. Runtime은 두 inventory digest가 일치할 때만 restore와 replay를 시작한다.

## 3. 예제

```csharp
services.AddZLinkFramework(options =>
{
    options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString("redis-host:6379") // 공식 Redis extension의 연결 정보를 설정한다.
        .SetKeyPrefix("zlink:game:location"))); // authority transaction domain을 분리한다.

    options.AddRelocationStore(new ZLinkRedisRelocationStore(redis => redis
        .SetConnectionString("redis-host:6379") // 같은 deployment를 선택할 수 있다.
        .SetKeyPrefix("zlink:game:relocation"))); // immutable payload key를 Location과 분리한다.

    options.AddRouteMesh("world")
        .Listen(7300)
        .SetRoutingId(nodeRid); // 자동 discovery와 분산 Spot·Actor가 같은 store를 사용한다.
});
```
