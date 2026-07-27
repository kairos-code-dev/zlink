# .NET 공식 Redis provider public API

[.NET exact interface 목차](README.ko.md) · [Location record](08-location-maintenance.ko.md) ·
[Authority와 relocation](08-authority-relocation.ko.md)

## 1. MeshNode change stamp

```csharp
// IZLinkLocationStore에 직접 포함되는 optional member signature fragment다.
// 전체 interface 선언은 08-location-maintenance.ko.md에 있다.
ValueTask<ulong?> GetMeshNodeChangeStampAsync(
    string meshName,
    CancellationToken cancellationToken = default) =>
    ValueTask.FromResult<ulong?>(null);
```

Change stamp는 `IZLinkLocationStore`에 포함된 선택적 최적화다. 별도 public capability interface나 DI 등록은
없다. Provider가 지원하지 않으면 기본 구현의 `null`을 반환하며 Framework는 polling으로 같은 상태에
수렴한다. Stamp는 같은 MeshName의 MeshNode descriptor 변경이 없을 때 목록 조회를 생략하는 최적화이며
correctness authority가 아니다. ClientServer·fanout·owner lease·authority용 public scope type은 제공하지 않는다.

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

두 Redis class가 공개하는 store operation은 구현한 provider SPI의 member와 정확히 같다. Redis 전용
authority·relocation operation은 추가하지 않는다.

각 options의 `KeyPrefix`와 `ConnectionString` 또는 `ConfigurationOptions` 가운데 하나는 필수다. 두 연결 설정을 모두
제공하면 `ConfigurationOptions`를 사용한다. Redis client connection은 각 store 인스턴스가 소유하며 host가
store를 dispose한다. Dispose가 시작된 뒤 새 operation은 `ObjectDisposedException`으로 실패하고, 이미 시작한
operation이 끝난 뒤 connection을 해제한다.

두 Store는 같은 Redis deployment 또는 cluster를 서로 다른 key prefix로 사용할 수 있고, 서로 다른 Redis를 사용할
수도 있다. 같은 deployment를 사용할 때는 운영 데이터와 정리 범위를 분리할 수 있도록 서로 다른 prefix를 사용하는
것을 권장한다. Connection 공유와 deployment identity 비교는 public correctness 계약이 아니다.

`ZLinkRedisLocationStore`는 `IZLinkLocationStore`의 descriptor·owner lease·authority·capacity member와
change stamp override를 같은 Redis connection과 key namespace에서 제공한다. 모든 operation은
[`IZLinkLocationStore`](08-location-maintenance.ko.md#5-provider-spi)와
[authority 계약](08-authority-relocation.ko.md#2-authority-operation과-data-type)의 결과·원자성·idempotency 규칙을 그대로
지킨다. Provider는 opaque authority payload를 해석하지 않으며 Spot kind나 relocation phase별 public method를
추가하지 않는다.

`ZLinkRedisRelocationStore`는 immutable relocation payload만 저장하고 Location authority나 participant
membership을 변경하지 않는다. `ZLinkRedisLocationStore`는 `IZLinkRelocationStore`를 함께 구현하지 않는다.

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
