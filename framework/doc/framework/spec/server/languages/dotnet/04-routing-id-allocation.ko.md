# .NET routing ID 자동 할당 공개 계약

[.NET 계약 목차](README.ko.md) · [RouteMesh·MeshNode](05-route-mesh.ko.md) ·
[Location Store·Redis](06-location-store.ko.md) ·
[Redis slot 원자성](../../41-location-store-redis.ko.md#8-routing-id-slot-원자성)

## 1. 범위

이 문서는 MeshNode routing ID slot allocation의 정확한 .NET store, 결과와 readiness provider
시그니처를 고정한다. MeshNode builder의 allocation 메서드는
[05 RouteMesh·MeshNode §2](05-route-mesh.ko.md#2-등록-인터페이스)가 소유하며 이 문서에서 재선언하지
않는다.

한 allocation group은 같은 slot 번호를 공유할 MeshNode member와 각 routing ID prefix를 묶는다. 같은
process가 서로 다른 MeshName의 MeshNode를 여러 개 등록하면 한 group에서 같은 slot을 각 member의 RID에
적용할 수 있다.

## 2. Store capability

```csharp
public interface IZLinkRoutingIdSlotAllocationStore
{
    ValueTask<ZLinkRoutingIdSlotAcquireResult> AcquireRoutingIdSlotAsync(
        ZLinkRoutingIdSlotAcquireRequest request,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRoutingIdSlotReleaseResult> ReleaseRoutingIdSlotAsync(
        string groupName,
        int slot,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRoutingIdSlotAllocationSnapshot> ListRoutingIdSlotsAsync(
        string groupName,
        CancellationToken cancellationToken = default);
}

public sealed record ZLinkRoutingIdSlotAcquireRequest(
    string GroupName,
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> Members,
    int SlotCount,
    string OwnerId,
    TimeSpan LeaseTtl);

public sealed record ZLinkRoutingIdSlotAllocationMember(
    string MeshName,
    string RoutingIdPrefix);

public abstract record ZLinkRoutingIdSlotAcquireResult
{
    private protected ZLinkRoutingIdSlotAcquireResult() { }
}

public sealed record ZLinkRoutingIdSlotAcquired(
    ZLinkRoutingIdSlotAllocation Allocation)
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotGroupExhausted
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotGroupConfigurationMismatch(
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> ExpectedMembers,
    int ExpectedSlotCount,
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> ActualMembers,
    int ActualSlotCount)
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotIdentityModeConflict
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotAllocation(
    int Slot,
    ZLinkLocationOwnerToken Owner,
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset StoreNow);

public enum ZLinkRoutingIdSlotReleaseResult
{
    Released = 1,
    IgnoredStale = 2
}

public sealed record ZLinkRoutingIdSlotAllocationSnapshot(
    string GroupName,
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> Members,
    int SlotCount,
    IReadOnlyList<ZLinkRoutingIdSlotAllocation> Allocations,
    DateTimeOffset StoreNow);
```

Acquire 결과 집합은 `ZLinkRoutingIdSlotAcquired`, `ZLinkRoutingIdSlotGroupExhausted`,
`ZLinkRoutingIdSlotGroupConfigurationMismatch`와 `ZLinkRoutingIdSlotIdentityModeConflict`로 닫혀 있다.
같은 owner의 재호출은 같은 slot과 generation을 반환한다. 서로 다른 owner가 같은 유효 slot을 동시에
받을 수 없다.

Release는 group, slot, owner와 generation이 모두 일치할 때만 `Released`다. 오래된 owner token은
`IgnoredStale`이며 현재 allocation을 바꾸지 않는다.
Acquire request, mismatch 결과와 snapshot의 `Members`는 `MeshName`, `RoutingIdPrefix` 순서로 ordinal
정렬한 immutable 목록이다. 같은 두 값을 가진 중복 member는 구성 오류로 거부한다. 따라서 호출자가
member를 등록한 순서는 group identity와 비교 결과에 영향을 주지 않는다.

## 3. Redis 등록

자동 할당은 별도 store registration을 제공하지 않는다. Root에 등록한 같은 `IZLinkLocationStore`
instance가 `IZLinkRoutingIdSlotAllocationStore`도 구현해야 한다. 공식 Redis location store가 production
capability를 제공한다.

자동 할당이 설정되어 있는데 등록한 location store가 이 interface를 구현하지 않으면 host startup이
`ZLinkConfigurationException`으로 실패한다.

## 4. 준비된 결과 조회

```csharp
public interface IZLinkAllocatedRoutingIdProvider
{
    ValueTask<ZLinkAllocatedRoutingId> WaitForReadyAllocationAsync(
        string groupName,
        CancellationToken cancellationToken = default);
}

public sealed record ZLinkAllocatedRoutingId(
    string GroupName,
    int Slot,
    IReadOnlyDictionary<string, RoutingId> MeshNodeRoutingIds);
```

Dictionary key는 MeshName이다. Provider는 group의 모든 MeshNode에 routing ID가 적용되고 bind, MeshNode
descriptor 게시와 readiness가 완료된 뒤 결과를 반환한다. 등록되지 않은 group은 `ZLinkConfigurationException`으로
실패한다.

## 5. Startup 순서

1. 모든 allocation group과 lease option을 검증한다.
2. group 이름 순서로 slot과 owner lease를 확보한다.
3. 확정한 RID를 각 MeshNode에 적용한다.
4. MeshNode socket을 만들고 bind한다.
5. MeshNode descriptor를 게시한 뒤 readiness provider를 완료한다.

어느 group이 소진되면 확보한 다른 group의 slot을 release하고 bind를 시작하지 않는다. Fixed routing ID와
automatic allocation을 같은 MeshNode에 설정하면 startup이 실패한다.

Lease option의 정확한 시그니처와 검증 관계는
[Location Store·Redis §2](06-location-store.ko.md#2-root-등록과-option)가 소유한다. Lease renew를 안전
기한까지 확인하지 못하면 host는 관련 MeshNode의 종료를 요청한다. 실행 중에 새 slot을 임의로 선택하지
않는다.
