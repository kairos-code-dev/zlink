# .NET Location Store·Redis 공개 인터페이스

[.NET 계약 목차](README.ko.md) · [Location Runtime](../../40-location-runtime.ko.md) ·
[Redis Location Store](../../41-location-store-redis.ko.md) · [routing ID 자동 할당](04-routing-id-allocation.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0의 .NET location store 확장 지점과 공식 Redis 구현의 정확한 공개
인터페이스를 고정한다. 하나의 store 인스턴스가 MeshNode descriptor, Spot·Actor 위치, owner lease와
Actor transfer authority를 함께 제공한다. Routing ID 자동 할당을 사용하면 같은 인스턴스가
`IZLinkRoutingIdSlotAllocationStore`도 구현해야 한다.

Location store는 application 데이터 저장소가 아니다. Framework runtime이 물리 MeshNode의 접속 정보와
논리 Spot·Actor의 현재 owner를 확인하고 generation으로 오래된 쓰기를 차단할 때만 사용한다.

## 2. Root 등록과 option

```csharp
public sealed class ZLinkLocationOptions
{
    public TimeSpan HeartbeatInterval { get; set; } = TimeSpan.FromSeconds(10);
    public TimeSpan OwnerLeaseTtl { get; set; } = TimeSpan.FromSeconds(30);
    public TimeSpan PollingInterval { get; set; } = TimeSpan.FromSeconds(1);
    public TimeSpan StoreFailureGrace { get; set; } = TimeSpan.FromSeconds(30);
    public TimeSpan RoutingIdFencingMargin { get; set; } = TimeSpan.FromSeconds(5);
    public TimeSpan OwnerLeaseRenewTimeout { get; set; } = TimeSpan.FromSeconds(3);
}
```

Root의 `AddLocationStore(IZLinkLocationStore)`와 `ConfigureLocations()` 시그니처는
[RouteMesh·MeshNode §2](05-route-mesh.ko.md#2-등록-인터페이스)가 한 번만 선언한다. 이 문서는 두 메서드가
사용하는 store와 option 타입을 소유한다.

Store는 host마다 정확히 하나만 등록할 수 있다. 자동 discovery, remote Spot·Actor 위치 확인 또는 Actor
transfer를 구성했는데 store를 등록하지 않으면 startup이 `ZLinkConfigurationException`으로 실패한다.
Manual peer와 process-local Spot·Actor만 사용하는 host는 store를 등록하지 않아도 된다.

모든 option은 0보다 커야 한다. Routing ID 자동 할당을 사용하면 다음 관계도 만족해야 한다.

```text
HeartbeatInterval + OwnerLeaseRenewTimeout
    < OwnerLeaseTtl - RoutingIdFencingMargin
```

## 3. 공통 쓰기 결과와 owner lease

```csharp
public enum ZLinkLocationWriteIntent
{
    NewClaim = 1,
    Renew = 2,
    Takeover = 3
}

public enum ZLinkLocationWriteStatus
{
    Stored = 1,
    IgnoredStale = 2,
    RejectedConflict = 3
}

public sealed record ZLinkLocationWriteResult(
    ZLinkLocationWriteStatus Status,
    ulong Generation,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkLocationOwnerToken(
    string OwnerId,
    ulong Generation);

public sealed record ZLinkOwnerLease(
    string OwnerId,
    RoutingId NodeRid,
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset UpdatedAt);

public sealed record ZLinkOwnerLeaseRenewal(
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset StoreNow);

public sealed record ZLinkOwnerLeaseSnapshot(
    IReadOnlyList<ZLinkOwnerLease> Leases,
    DateTimeOffset StoreNow);
```

`NewClaim`은 record가 없거나 이전 owner lease가 만료된 경우에만 새 generation을 발급한다. `Renew`와
owner-guarded remove는 owner ID와 generation이 현재 record와 정확히 일치해야 한다. `Takeover`는 store가
현재 owner lease 만료를 같은 원자 operation에서 확인할 수 있을 때만 성공한다. 예상되는 경합은
`ZLinkLocationWriteStatus`로 반환하고 store 접속·명령 실패는 `ZLinkLocationStoreException`으로 보고한다.

```csharp
public sealed class ZLinkLocationStoreException : Exception
{
    public bool IsRetriable { get; }
}
```

## 4. Descriptor와 location record

```csharp
public sealed record ZLinkMeshNodeDescriptor(
    string MeshName,
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    IReadOnlyDictionary<string, int> ChannelWeights,
    bool Draining,
    string SecurityIdentity,
    string OwnerId,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkMeshNodeDescriptorKey(
    string MeshName,
    RoutingId Rid);

public sealed record ZLinkSpotLocation(
    string MeshName,
    RoutingId SpotRid,
    ulong SpotGeneration,
    RoutingId OwnerNodeRid,
    ulong OwnerNodeGeneration,
    ZLinkSpotKind SpotKind,
    string SpotType,
    string OwnerId,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkSpotLocationKey(
    string MeshName,
    RoutingId SpotRid);

public sealed record ZLinkActorLocation(
    string MeshName,
    string ActorId,
    string ActorType,
    ActorRef ActorRef,
    RoutingId OwnerNodeRid,
    ulong OwnerNodeGeneration,
    RoutingId SpotRid,
    ZLinkSpotKind SpotKind,
    ulong MembershipEpoch,
    string OwnerId,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkActorLocationKey(
    string MeshName,
    string ActorId);
```

`ChannelWeights`의 key 집합은 descriptor를 처음 게시하기 전에 고정한 ChannelName membership과 같다.
Weight 값과 drain 상태는 실행 중 바뀔 수 있지만 membership은 바뀌지 않는다. Spot과 Actor record는 owner
MeshNode의 RID와 generation을 함께 보존한다. Resolver는 owner lease와 같은 generation의 descriptor가
모두 유효할 때만 record를 성공 결과로 사용한다.

## 5. Store capability

```csharp
public interface IZLinkLocationStore :
    IZLinkMeshNodeLocationStore,
    IZLinkSpotLocationStore,
    IZLinkActorLocationStore,
    IZLinkOwnerLeaseStore,
    IZLinkActorTransferStore
{
    ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default);
}

public interface IZLinkMeshNodeLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation location,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteStatus> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation location,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default);
}

public interface IZLinkOwnerLeaseStore
{
    ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default);
    ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default);
}
```

`ListMeshNodesAsync`는 한 번의 store snapshot에서 같은 MeshName의 descriptor를 반환하며 pagination하지
않는다. Framework는 transport admission을 완료하기 전에는 반환된 descriptor를 ready peer로 간주하지
않는다. Spot과 Actor resolve가 `null`을 반환하면 현재 유효한 record가 없다는 뜻이다.

## 6. Actor transfer authority

```csharp
public enum ZLinkActorTransferState
{
    Prepared = 1,
    Committed = 2,
    Activated = 3,
    Aborted = 4
}

public sealed record ZLinkActorTransferRecord(
    string MeshName,
    string ActorId,
    Guid TransferId,
    ActorRef Source,
    ActorRef Target,
    ulong ExpectedActorGeneration,
    ulong ExpectedMembershipEpoch,
    IReadOnlySet<RoutingId> Participants,
    ZLinkActorTransferState State,
    string RecoveryOwnerId,
    DateTimeOffset RecoveryLeaseExpiresAt,
    DateTimeOffset UpdatedAt);

public sealed record ZLinkActorTransferPrepareRequest(
    string MeshName,
    string ActorId,
    Guid TransferId,
    ActorRef Source,
    ActorRef Target,
    ulong ExpectedActorGeneration,
    ulong ExpectedMembershipEpoch,
    IReadOnlySet<RoutingId> Participants,
    string RecoveryOwnerId,
    TimeSpan RecoveryLeaseTtl);

public enum ZLinkActorTransferWriteStatus
{
    Stored = 1,
    NotFound = 2,
    IgnoredStale = 3,
    RejectedConflict = 4,
    InvalidState = 5
}

public sealed record ZLinkActorTransferWriteResult(
    ZLinkActorTransferWriteStatus Status,
    ZLinkActorTransferRecord? Record);

public interface IZLinkActorTransferStore
{
    ValueTask<ZLinkActorTransferWriteResult> PrepareActorTransferAsync(
        ZLinkActorTransferPrepareRequest request,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorTransferWriteResult> CommitActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorTransferWriteResult> ActivateActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorTransferWriteResult> AbortActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorTransferWriteResult> TakeOverActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string successorOwnerId,
        TimeSpan recoveryLeaseTtl,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorTransferRecord?> ResolveActorTransferAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default);
}
```

`Guid TransferId`는 Redis 경계에서 UUID 128-bit의 소문자 `8-4-4-4-12` 문자열로 변환한다. 읽을 때도 이
형식만 받아 같은 `Guid` 값으로 복원하며 다른 문자열 표현을 store record로 허용하지 않는다.

Prepare는 현재 Actor owner·generation·membership epoch와 active transfer 부재를 하나의 원자 operation에서
확인한다. Commit은 Actor location을 target owner와 정확히 다음 membership epoch로 바꾸고 record를
`Committed`로 전이한다. Activate와 abort는 각각 허용된 직전 상태에서만 성공한다. Takeover는 기존
recovery lease 만료, participant set과 현재 Actor location을 같은 operation에서 확인한다.

이 interface는 Core가 발급한 sealed token을 받지 않는다. Framework transfer coordinator는 Redis의
분산 권한 결정과 같은 process의 Core prepare·commit을 함께 조정한다. Store 구현은 application이 만든
token이나 외부 token 검증 callback을 요구할 수 없다.

## 7. Change stamp

```csharp
public enum ZLinkLocationChangeScopeKind
{
    MeshNode = 1,
    Spot = 2,
    Actor = 3,
    OwnerLease = 4,
    ActorTransfer = 5
}

public readonly record struct ZLinkLocationChangeStampScope(
    ZLinkLocationChangeScopeKind Kind,
    string? MeshName);

public interface IZLinkLocationChangeStampStore
{
    ValueTask<ulong> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default);
}
```

Change stamp는 선택 capability다. 등록한 store가 이 interface를 구현하지 않아도 polling으로 같은 상태에
수렴해야 한다. Stamp는 변경이 없을 때 목록 조회를 생략하는 최적화이며 correctness authority가 아니다.

## 8. 공식 Redis package

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

## 9. 예제

```csharp
services.AddZLinkFramework(options =>
{
    options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString("redis-host:6379") // 공식 Redis extension의 연결 정보를 설정한다.
        .SetKeyPrefix("zlink:game")));          // 다른 배포와 key namespace를 분리한다.

    options.AddRouteMesh("world")
        .Listen("tcp://0.0.0.0:7300")
        .SetRoutingId(nodeRid); // 자동 discovery와 분산 Spot·Actor가 같은 store를 사용한다.
});
```
