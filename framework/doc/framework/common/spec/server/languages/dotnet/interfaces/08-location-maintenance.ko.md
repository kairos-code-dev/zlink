# .NET Location public API와 provider SPI

[.NET exact interface 목차](README.ko.md) · [Location Runtime](../../../../40-location-runtime.ko.md) ·
[Redis Location Store](../../../../41-location-store-redis.ko.md) · [configuration과 topology](03-configuration-topology.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0의 .NET location·authority store 확장 지점의 정확한 공개 인터페이스를
고정한다. 하나의 store 인스턴스가 MeshNode descriptor, ClientServer server descriptor, fanout publisher
descriptor, Spot·Actor 위치, owner lease, durable object [authority](../../../../01-glossary.ko.md#authority)와 placement reservation을 함께 제공한다.
Routing ID의 active 충돌 확인은 [MeshNode descriptor](../../../../01-glossary.ko.md#meshnode-descriptor)의 owner CAS가 담당하며 별도 slot store를 등록하지 않는다.

Location store는 application 데이터 저장소가 아니다. Framework runtime이 물리 MeshNode의 접속 정보와
논리 [Spot](../../../../01-glossary.ko.md#spot)·Actor의 현재 [owner](../../../../01-glossary.ko.md#owner)를 확인하고 generation으로 오래된 쓰기를 차단할 때만 사용한다.

## 2. Root 등록과 option

```csharp
public sealed class ZLinkLocationOptions
{
    public TimeSpan OwnerLeaseRenewInterval { get; set; } = TimeSpan.FromSeconds(5);
    public TimeSpan OwnerLeaseTtl { get; set; } = TimeSpan.FromSeconds(15);
    public TimeSpan PollingInterval { get; set; } = TimeSpan.FromSeconds(1);
    public TimeSpan StoreFailureGrace { get; set; } = TimeSpan.FromSeconds(30);
    public TimeSpan OwnerLeaseFencingMargin { get; set; } = TimeSpan.FromSeconds(5);
    public TimeSpan OwnerLeaseRenewTimeout { get; set; } = TimeSpan.FromSeconds(3);
    public TimeSpan RouteCacheMaxAge { get; set; } = TimeSpan.FromSeconds(15);
    public TimeSpan RelocationForwardingWindow { get; set; } = TimeSpan.FromSeconds(30);
    public int MaxActiveOutboundRelocations { get; set; } = 64;
    public int MaxActiveInboundRelocations { get; set; } = 64;
    public int MaxConcurrentRelocationCaptures { get; set; } = 8;
    public int MaxConcurrentRelocationRestores { get; set; } = 8;
    public long MaxRelocationPayloadInFlightBytes { get; set; } = 268_435_456;
}
```

Root의 `AddLocationStore(IZLinkLocationStore)`와 `ConfigureLocations()` 시그니처는
[Topology configuration §2](03-configuration-topology.ko.md#2-등록-인터페이스)가 한 번만 선언한다. 이 문서는 두 메서드가
사용하는 store와 option 타입을 소유한다.

Store는 host마다 정확히 하나만 등록할 수 있다. Object role이 `Client` 또는 `Server`인 [MeshNode](../../../../01-glossary.ko.md#meshnode), automatic
discovery, fanout publisher descriptor 또는 stateful relocation을 구성했는데 store를 등록하지 않으면 startup이
`ZLinkConfigurationException`으로 실패한다. Store를 등록하지 않은 fanout publisher는 manual endpoint
대상으로 동작한다. Object role `None`인 manual MeshNode와 manual fanout publisher·subscriber는 store를 등록하지
않아도 된다. `None`은 manager, factory, placement와 hidden local object runtime을 만들지 않는다.

Lease와 polling option은 0보다 커야 한다. `RouteCacheMaxAge`와 `RelocationForwardingWindow`는 0 이상이다.
둘 다 양수이면 cache age가 forwarding window보다 최소 5초 작아야 한다. 0인 값은 각각 cache 또는 forwarding을
끈다. 실행 중 변경은 새 cache entry와 새 relocation에만 적용한다. [Location Store](../../../../01-glossary.ko.md#location-store)와 [owner lease](../../../../01-glossary.ko.md#owner-lease) runtime을 사용하는
모든 host는 [routing ID](../../../../01-glossary.ko.md#routing-id) mode와 관계없이 다음 관계를 만족해야 한다.

```text
OwnerLeaseRenewInterval + OwnerLeaseRenewTimeout
    < OwnerLeaseTtl - OwnerLeaseFencingMargin
```

`OwnerLeaseRenewInterval`은 owner lease 갱신 주기를 정하며 service connection의 liveness interval이 아니다.
Application traffic과 무관한 5초 periodic probe와 같은 current connection의 matching ACK 15초 deadline은 이
option에 포함하지 않으며 Framework public API로 설정하지 않는다. 다른 inbound frame은 [deadline](../../../../01-glossary.ko.md#deadline)을 충족하지
않는다.

`StoreFailureGrace`는 discovery reconcile과 새 outbound connect에만 적용한다. Store failure 동안 마지막 stable
desired set을 grace까지 고정하고 existing admitted transport에는 service liveness를 계속 적용한다. Grace 뒤에는
stable store snapshot을 다시 얻기 전까지 새 connection을 만들지 않는다. 이 값은 owner·coordinator lease나 local
authority deadline을 연장하지 않으며 stateful message, timer, [factory](../../../../01-glossary.ko.md#factory)와 CAS admission은 마지막 valid monotonic
lease deadline에서 닫힌다. Recovery는 exact owner token과 stable page set을 재검증한 뒤 diff와 connect를 수행한다.

다섯 relocation 제한 option은 모두 양수여야 하며 process 전체의 Actor·Spot relocation에 적용한다.
Outbound와 inbound active unit은 각각 최대 64개이고, Capture와 Restore callback은 각각 최대 8개를 동시에
실행한다. Payload 단계에서 encoded bytes의 합은 기본 268,435,456 bytes(256 MiB)를 넘지 않는다. 이 byte 합에는
application state, 실행하지 않은 message queue, Actor accepted journal, timer의 logical registration과 pending tick,
relocation manifest와 Framework metadata를 모두 포함한다. User Spot aggregate 하나가 byte 한도를 넘으면 다른
relocation payload 단계와 겹치지 않는 동안에만 단독으로 진행할 수 있다.

Framework는 active unit, callback과 예상 payload byte permit을 모두 확보하기 전에는 source Actor·Spot queue를
seal하지 않는다. Permit을 기다리는 동안 source는 application message와 timer dispatch를 계속 처리한다. 실행 중
option 변경은 새 relocation admission에만 적용하며 이미 permit을 확보한 unit의 한도를 줄이지 않는다.

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
    DateTimeOffset UpdatedAt)
{
    public static ZLinkLocationWriteResult IgnoredStale { get; }
    public static ZLinkLocationWriteResult RejectedConflict { get; }
    public static ZLinkLocationWriteResult Stored(
        ulong generation,
        DateTimeOffset updatedAt);
}

public readonly record struct ZLinkLocationOwnerToken(
    string OwnerId,
    long LeaseGeneration);

public abstract record ZLinkOwnerLeaseClaimResult
{
    private protected ZLinkOwnerLeaseClaimResult() { }
    public sealed record Claimed(
        ZLinkLocationOwnerToken Token,
        DateTimeOffset LeaseExpiresAt,
        DateTimeOffset StoreNow) : ZLinkOwnerLeaseClaimResult;
    public sealed record Conflict : ZLinkOwnerLeaseClaimResult;
    public sealed record GenerationExhausted : ZLinkOwnerLeaseClaimResult;
}

public abstract record ZLinkOwnerLeaseRenewResult
{
    private protected ZLinkOwnerLeaseRenewResult() { }
    public sealed record Renewed(
        DateTimeOffset LeaseExpiresAt,
        DateTimeOffset StoreNow) : ZLinkOwnerLeaseRenewResult;
    public sealed record Stale : ZLinkOwnerLeaseRenewResult;
}

public enum ZLinkOwnerLeaseReleaseResult
{
    Released = 0,
    Stale = 1
}

public abstract record ZLinkOwnerLeaseReadResult
{
    private protected ZLinkOwnerLeaseReadResult() { }
    public sealed record Found(
        ZLinkLocationOwnerToken Token,
        DateTimeOffset LeaseExpiresAt,
        DateTimeOffset StoreNow) : ZLinkOwnerLeaseReadResult;
    public sealed record Missing : ZLinkOwnerLeaseReadResult;
}

```

`NewClaim`은 record가 없거나 이전 owner lease가 만료된 경우에만 새 generation을 발급한다. `Renew`와
owner-guarded remove는 owner ID와 generation이 현재 record와 정확히 일치해야 한다. `Takeover`는 store가
현재 owner lease 만료를 같은 원자 operation에서 확인할 수 있을 때만 성공한다. 예상되는 경합은
`ZLinkLocationWriteStatus`로 반환한다. Store 접속·명령 실패는 provider가 발생시킨 exception을 그대로 전달한다.
Framework의 health·discovery·recovery 경계가 timeout과 재시도 정책을 적용하므로 provider가 구현해야 하는
별도의 public exception type이나 `IsRetriable` 분류는 제공하지 않는다.

## 4. Provider descriptor와 capacity DTO

```csharp
public sealed record ZLinkMeshNodeDescriptor(
    string MeshName,
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    IReadOnlyDictionary<string, int> ChannelWeights,
    string SecurityIdentity,
    string OwnerId,
    long LeaseGeneration,
    DateTimeOffset UpdatedAt)
{
    public long ApplicationVersion { get; init; }
    public IReadOnlyList<ZLinkObjectCapability> ObjectCapabilities { get; init; }
        = Array.Empty<ZLinkObjectCapability>();
    public string? MaintenanceWave { get; init; }
    public ZLinkFrameworkRuntimeState State { get; init; }
    public ZLinkMeshNodeObjectRole ObjectRole { get; init; }
    public string? EntrySpotId { get; init; }
    public int PlacementWeight { get; init; } = 100;
    public ZLinkPlacementCapacity Capacity { get; init; }
        = new(new(0, 0, 0), new(0, 0, 0), Array.Empty<ZLinkSpotTypeCapacity>());
    public ZLinkActivationConcurrency ActivationConcurrency { get; init; }
        = new(0, 128);
}

public readonly record struct ZLinkMeshNodeDescriptorKey(
    string MeshName,
    RoutingId Rid);

public sealed record ZLinkClientServerServerDescriptor(
    string ChannelName,
    RoutingId ServerRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    int Weight,
    ZLinkFrameworkRuntimeState State,
    string SecurityIdentity,
    string OwnerId,
    long LeaseGeneration,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkClientServerServerDescriptorKey(
    string ChannelName,
    RoutingId ServerRid);

public sealed record ZLinkFanoutPublisherDescriptor(
    string ChannelName,
    RoutingId PublisherRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    ZLinkFrameworkRuntimeState State,
    string SecurityIdentity,
    string OwnerId,
    long LeaseGeneration,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkFanoutPublisherDescriptorKey(
    string ChannelName,
    RoutingId PublisherRid);

public enum ZLinkMeshNodeObjectRole
{
    None = 0,
    Client = 1,
    Server = 2
}

public enum ZLinkPlacementObjectKind
{
    Actor = 1,
    UserSpot = 2,
    InstanceSpot = 3
}

public enum ZLinkObjectMaintenancePolicyKind
{
    Disabled = 1,
    Recreate = 2,
    Snapshot = 3
}

public sealed record ZLinkObjectCapability(
    ZLinkPlacementObjectKind ObjectKind,
    string StableType,
    ZLinkObjectMaintenancePolicyKind Policy,
    bool HasSnapshotAdapter,
    int Limit);

public sealed record ZLinkPopulationCapacity(
    int Active,
    int Reserved,
    int Limit);

public sealed record ZLinkSpotTypeCapacity(
    ZLinkPlacementObjectKind ObjectKind,
    string StableType,
    int Active,
    int Reserved,
    int Limit);

public sealed record ZLinkPlacementCapacity(
    ZLinkPopulationCapacity Actors,
    ZLinkPopulationCapacity Spots,
    IReadOnlyList<ZLinkSpotTypeCapacity> SpotTypes);

public sealed record ZLinkActivationConcurrency(
    int Active,
    int Limit);
```

`ChannelWeights`의 key 집합은 [descriptor](../../../../01-glossary.ko.md#descriptor)를 처음 게시하기 전에 고정한 ChannelName membership과 같다.
Stable type은 UTF-8 byte 순서로 정렬한다.
`ObjectCapabilities`는 Actor·User Spot·Instance Spot의 [stable type](../../../../01-glossary.ko.md#stable-type), policy와 [Snapshot](../../../../01-glossary.ko.md#relocation-policy) adapter 등록 여부를
한 항목에 함께 둔다. User·Instance Spot capability의 `Limit`은 stable type별 limit이고 Actor는
`0`이다. `HasSnapshotAdapter`는 target에 해당 object kind의 adapter가
등록되어 있는지만 나타내며 application state의 format, version이나 contract ID를 광고하지 않는다.
`ApplicationVersion`은 `0..long.MaxValue`다.
`Capacity`는 Actor 전체, Spot 전체와 등록한 User·Instance Spot type별 active·reserved·limit projection을
구분한다. `ActivationConcurrency`는 population reservation과 분리된 process-local active·limit
projection이다. Channel weight, placement [weight](../../../../01-glossary.ko.md#weight), capacity count,
maintenance wave와 runtime state는 실행 중 바뀔 수 있다.

Descriptor의 key, RID, lifecycle generation, endpoint, security identity, owner token, application version,
[ChannelName](../../../../01-glossary.ko.md#channelname) key set, object role, population limit,
activation concurrency limit과 object capability의 kind·type·policy·Snapshot adapter 등록 여부·limit은
첫 admission 뒤 해당 lifecycle에서 바뀌지 않는다. Channel weight 값, placement weight,
active·reserved count, activation active count, maintenance wave와 runtime state만
mutable하다. Mutable update는 current owner token과 같은 [lifecycle generation](../../../../01-glossary.ko.md#lifecycle-generation)을 제시하고
`DescriptorRevision`을 strictly 증가시켜야 한다. Provider는 stale revision이나 immutable field 변경을 원자적으로
거부하며 일부 field만 적용하지 않는다. ClientServer와 fanout descriptor도 같은 identity·revision fence를
적용한다.

`ZLinkClientServerServerDescriptor`는 MeshName과 RouteMesh [membership](../../../../01-glossary.ko.md#membership)을 갖지 않는다.
`ZLinkFanoutPublisherDescriptor`는 subscriber나 target weight를 갖지 않는다.

## 5. Provider SPI

Application은 이 interface를 호출하지 않는다. Location provider 구현자는 discovery descriptor, owner lease,
object authority와 placement transaction을 모두 제공하는 `IZLinkLocationStore` 하나를 구현한다. 일부 기능만
구현한 provider를 조합하지 않으므로 startup 시 capability downcast나 별도 Store 등록이 없다. Relocation
payload는 [별도의 `IZLinkRelocationStore`](08-authority-relocation.ko.md#3-relocation-store)가 맡는다.

```csharp
public interface IZLinkLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        ZLinkPageRequest page,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> UpdateClientServerAsync(
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteStatus> RemoveClientServerAsync(
        ZLinkClientServerServerDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationPage<ZLinkClientServerServerDescriptor>> ListClientServersAsync(
        string channelName,
        ZLinkPageRequest page,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> UpdateFanoutPublisherAsync(
        ZLinkFanoutPublisherDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteStatus> RemoveFanoutPublisherAsync(
        ZLinkFanoutPublisherDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>> ListFanoutPublishersAsync(
        string channelName,
        ZLinkPageRequest page,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkOwnerLeaseClaimResult> ClaimOwnerLeaseAsync(
        string ownerId,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkOwnerLeaseRenewResult> RenewOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkOwnerLeaseReleaseResult> ReleaseOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken = default);

    // Authority DTO는 08-authority-relocation.ko.md에 정의한다.
    ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkAuthorityCompareExchangeResult> CompareExchangeAuthorityAsync(
        ZLinkAuthorityKey key,
        string expectedStoreVersion,
        ZLinkAuthorityMutation mutation,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
        string prefix,
        ZLinkAuthorityScanCursor? cursor,
        int limit,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkObjectReserveResult> ReserveAsync(
        ZLinkObjectReservationRequest request,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkObjectCommitResult> CommitAsync(
        ZLinkObjectReservation reservation,
        ReadOnlyMemory<byte> readyPayload,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkObjectCreationCompleteResult> CompleteCreationAsync(
        ZLinkObjectReservation reservation,
        ZLinkObjectCreationCompletion completion,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkCreationTerminalReadResult> ReadCreationTerminalAsync(
        ZLinkCreationOperationId operation,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkObjectAbortResult> AbortAsync(
        ZLinkObjectReservation reservation,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkRelocationCapacityReserveResult> ReserveRelocationCapacityAsync(
        ZLinkRelocationCapacityReservationRequest request,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkRelocationCapacityAbortResult> AbortRelocationCapacityAsync(
        ZLinkRelocationCapacityFence fence,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
        ZLinkAggregatePrepareRequest request,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default);

    ValueTask<long> RemoveAllByOwnerAsync(
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    // MeshNode change stamp를 지원하지 않으면 기본 구현의 null을 반환한다.
    ValueTask<ulong?> GetMeshNodeChangeStampAsync(
        string meshName,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult<ulong?>(null);
}
```

Framework는 host process runtime lifecycle마다 새 owner ID를 만들며 application이 값을 설정하거나
이전 lifecycle의 ID를 재사용하게 하지 않는다. 한 host의 모든 MeshNode·ClientServer·fanout
descriptor와 authority는 같은 host token을 참조하고 각 descriptor가 자신의 RID를 갖는다.
`ClaimOwnerLeaseAsync`만 owner token을 발급한다. Provider domain은 영구적인 global
lease generation counter 하나를 유지하고 claim이 성공할 때마다 증가시켜 `1..long.MaxValue`의
token을 발급한다. Active owner ID의 중복 claim은 `Conflict`다. Expiry·release는 active row를 삭제하며
같은 owner ID로 다시 claim하면 더 큰 global generation을 받는다. Renew와 release는 token 전체가
current claim과 같을 때만 성공하며 만료되었거나 교체된 token은 `Stale`이다. Descriptor와 page item의
`LeaseGeneration`은 current token의 `LeaseGeneration`과 같아야 한다. Target admission 직전에
`ReadOwnerLeaseAsync(ownerId)`로 exact token을 다시 확인한다. Owner lease 전체 목록과 snapshot type은 public
surface에 제공하지 않는다.

Global lease generation counter가 `long.MaxValue`에 도달한 뒤 새 generation이 필요한 claim은
`GenerationExhausted`를 반환한다. 만료된 row를 takeover하는 claim도 같은 규칙을 적용한다. 이 결과는
retriable conflict나 provider exception이 아니며 row·index·counter를 바꾸지 않고 generation 값을 소비하지
않는다. 외부 상태가 바뀌지 않은 채 같은 expectation으로 다시 호출하면 같은 결과를 반환한다. Renew와
release에는 새 generation이 필요하지 않으므로 이 결과를 추가하지 않는다.

`IZLinkLocationStore` 하나가 descriptor, owner lease, authority와 capacity operation을 모두 제공한다. Root에는
이 Store를 정확히 한 번 등록하며 같은 provider transaction domain이 owner와 relocation authority를 원자적으로
compare-exchange한다. ClientServer와 fanout을 포함한 일부 capability만 구현하는 public interface는 제공하지
않는다. Change stamp는 선택적 최적화다. Provider가 `GetMeshNodeChangeStampAsync`에서 `null`을 반환하면 Framework는
descriptor page를 매 polling tick마다 읽으며 correctness는 달라지지 않는다.

Descriptor enumeration은 `ZLinkPageRequest`와 `ZLinkLocationPage<T>`를 사용한다. Effective `PageSize`는
`1..1000`이고 continuation token은 provider만 해석하는 opaque value다. Provider는 encoded page가 4 MiB에
먼저 도달하면 요청보다 적은 item과 다음 token을 반환하며 byte limit public option은 제공하지 않는다.
Stamp를 지원하는 provider는 같은 MeshName의 descriptor가 바뀔 때 값을 증가시킨다. `null`을 반환하는
provider는 stamp 최적화를 지원하지 않는다.
`SpotRef.ObjectGeneration`과 `ActorRef.ObjectGeneration`은 provider의 `ObjectGeneration`을 그대로 사용한다.
Authority envelope의 `AuthorityOwnerGeneration`은 authority owner 이관 fence이고 descriptor의
`LeaseGeneration`은 host lease fence다. 두 generation을 합치거나 Framework 계산값으로 만들지 않는다.

`LifecycleGeneration`은 0이 아닌 opaque equality token이다. Runtime은 수치 크기로 lifecycle의 선후를
판정하지 않는다. Application이 값을 선택하는 option은 없으며 manual descriptor도 Framework가 발급한 opaque
token과 current connection handover fence를 함께 사용한다. 순서를 비교하는 값은 `DescriptorRevision`뿐이다. 이 revision이
`long.MaxValue`인 상태에서 다음 값이 필요하면 host를 `Error`로 seal하고 wrap하지 않는다. Actor authority key는
`ActorId` 하나이고 Spot authority key는 `SpotId` 하나다. 두 key에 `MeshName`을 넣지 않는다.
Maintenance owner 이관은 `NewOwner`로 owner generation만 바꾸고 object generation을 유지한다.
기존 ref의 object generation은 유지된다. 이전 owner route를 사용하면 runtime이 current authority를 재조회하여
forwarding 또는 retry한다. Explicit close 뒤 cold recreate는 이전 row의 fenced delete가 완료된 후 새
`ReserveAsync`가 새 object generation을 발급한다. 이전 ref snapshot은 영구적으로 stale다.

`ListClientServersAsync`는 같은 ChannelName의 유효한 [ClientServer server descriptor](../../../../01-glossary.ko.md#clientserver-server-descriptor)를 반환한다. Framework는
Server RID와 generation을 admission에서 확인하기 전에는 반환된 descriptor를 ready target으로 사용하지
않는다.

`ListFanoutPublishersAsync`는 같은 ChannelName의 유효한 owner lease를 가진 publisher를 반환한다.
`Serving`이 아닌 publisher, current lifetime과 다른 lifecycle generation, 낮은 descriptor revision을 제외하는
책임은 automatic
subscriber의 connection-intent 계산에 있다. Automatic subscriber는 선택한 모든 endpoint를 연결 대상으로
사용하며 subscriber row는 게시하지 않는다.

Entry·User·[Instance Spot](../../../../01-glossary.ko.md#entry-spot-user-spot과-instance-spot) owner state는 global `SpotId`에서 파생한 하나의 authority key를 공유한다.
User Spot create와 Instance cold claim은 같은 row에 generic placement reserve를 수행하므로 kind conflict,
object generation과 capacity 증가가 원자적으로 결정된다. Actor relocation도 같은 generic authority capability의
별도 key를 사용한다. Provider는 Spot kind, owner state나 relocation phase를 해석하지 않는다.

Application service는 authority provider interface를 직접 호출하지 않는다. Authority key와 payload의 정확한
구성은 [Authority와 relocation](08-authority-relocation.ko.md)가 소유한다.

## 6. Location operational query

```csharp
public sealed record ZLinkLocationRuntimeStatus(
    bool StoreHealthy,
    bool WatchEnabled,
    TimeSpan PollingInterval,
    DateTimeOffset? LastRefreshAt,
    string? LastError,
    bool OwnerLeaseHealthy,
    DateTimeOffset? OwnerLeaseRenewedAt);

public enum ZLinkLocationTopologyState
{
    Discovered = 1,
    Connecting = 2,
    Ready = 3,
    Lost = 4,
    Error = 5,
    Stopped = 6
}

public sealed record ZLinkLocationTopologyFilter(
    string? MeshName = null,
    RoutingId? NodeRid = null,
    ZLinkLocationTopologyState? State = null);

public sealed record ZLinkLocationTopologyEntry(
    string MeshName,
    RoutingId NodeRid,
    string Endpoint,
    bool Draining,
    ZLinkLocationTopologyState State,
    DateTimeOffset UpdatedAt);

public sealed record ZLinkLocationServiceSummaryFilter(
    string? MeshName = null);

public sealed record ZLinkLocationServiceSummary(
    string MeshName,
    uint TotalCount,
    uint ReadyCount,
    uint ErrorCount,
    uint StoppedCount,
    DateTimeOffset LastUpdatedAt);

public readonly record struct ZLinkPageRequest(
    int PageSize = 100,
    string? ContinuationToken = null);

public sealed record ZLinkLocationPage<T>(
    IReadOnlyList<T> Items,
    string? ContinuationToken);

public interface IZLinkLocationReadiness
{
    ValueTask<bool> IsPeerReadyAsync(
        string meshName,
        ZLinkLocationRole role,
        RoutingId? nodeRid = null,
        CancellationToken cancellationToken = default);
}

public interface IZLinkLocationRuntimeQuery
{
    ValueTask<ZLinkLocationRuntimeStatus> GetStatusAsync(
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> ListMeshNodeDescriptorsAsync(
        string meshName,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationPage<ZLinkLocationTopologyEntry>> ListTopologyAsync(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationPage<ZLinkLocationServiceSummary>> ListServiceSummariesAsync(
        ZLinkLocationServiceSummaryFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}

public enum ZLinkLocationRole : ushort
{
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6
}

```
