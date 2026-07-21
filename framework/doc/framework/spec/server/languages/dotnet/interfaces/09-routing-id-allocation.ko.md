# .NET routing ID 자동 할당 공개 계약

[.NET exact interface 목차](README.ko.md) · [Topology configuration](03-configuration-topology.ko.md) ·
[Location record](08-location-maintenance.ko.md) ·
[Redis routing ID allocation](../../../41-location-store-redis.ko.md#7-routing-id-allocation)

## 1. 범위

이 문서는 MeshNode와 fanout publisher routing ID slot allocation의 정확한 .NET store, 결과와 readiness
provider 시그니처를 고정한다. Builder의 allocation 메서드는
[Topology configuration §2](03-configuration-topology.ko.md#2-등록-인터페이스)가 소유하며 이 문서에서 재선언하지
않는다.

한 allocation group은 같은 slot 번호를 공유할 MeshNode 또는 fanout publisher member와 각 routing ID
prefix를 묶는다. 같은 process의 여러 registration은 한 group에서 확정한 slot을 각 member의 RID에 적용할
수 있다.

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
    ZLinkLocationOwnerToken Owner);

public sealed record ZLinkRoutingIdSlotAllocationMember(
    string MeshName,
    string RoutingIdPrefix);

public sealed record ZLinkFanoutRoutingIdSlotAllocationMember(
    string ChannelName,
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
같은 owner token의 재호출은 같은 slot과 token을 반환한다. 서로 다른 owner가 같은 유효 slot을 동시에
받을 수 없다.

Host는 lifecycle 시작에서 owner lease를 한 번 claim한 뒤 같은 `ZLinkLocationOwnerToken`을 모든
acquire request에 넘긴다. Provider는 active host token을 slot 배정과 같은 원자 operation에서 확인한다.
Slot은 별도 TTL이나 token을 발급하지 않고 성공 result에는 입력과 같은 token만 반환한다.

`SlotCount`와 allocation `Slot`은 `1..65535`이고 `Members.Count`는 `1..255`다. 범위를 벗어난 builder
설정과 acquire request는 `ZLinkConfigurationException` 또는 provider validation error로 거부한다.
`ListRoutingIdSlotsAsync`는 이 상한 안에서 group 전체를 한 coherent snapshot으로 반환하며 pagination하지
않는다.

Release는 group, slot, owner와 generation이 모두 일치할 때만 `Released`다. 오래된 owner token은
`IgnoredStale`이며 현재 allocation을 바꾸지 않는다.
Acquire request, mismatch 결과와 snapshot의 `Members`는 MeshName과 routing ID prefix 순서로 정렬한
immutable 목록이다. 같은 MeshName의 중복 member는 구성 오류로 거부한다. 따라서 호출자가 member를
등록한 순서는 group identity와 비교 결과에 영향을 주지 않는다. Fanout publisher allocation은 같은
MeshNode member constructor를 바꾸지 않고 별도 fanout member와 allocation 결과로 제공한다.

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
    IReadOnlyDictionary<string, RoutingId> MemberRoutingIds);

public sealed record ZLinkFanoutAllocatedRoutingId(
    string GroupName,
    int Slot,
    IReadOnlyDictionary<string, RoutingId> PublisherRoutingIds);
```

`MemberRoutingIds`의 key는 MeshName이다. Provider는 group의 모든 MeshNode에 routing ID가 적용되고 bind와
descriptor 게시가 끝나 readiness에 도달한 뒤 결과를 반환한다. Fanout publisher 결과의 key는
ChannelName이다. 등록되지 않은 group은 `ZLinkConfigurationException`으로 실패한다.

## 5. Startup 순서

1. 모든 allocation group과 lease option을 검증한다.
2. Host owner lease를 lifecycle당 한 번 claim한다.
3. group 이름 순서로 같은 owner token을 검증하며 slot을 확보한다.
4. 확정한 RID를 각 MeshNode에 적용하고 socket을 bind한다.
5. MeshNode descriptor를 게시한 뒤 readiness provider를 완료한다.

어느 group이 소진되면 확보한 다른 group의 slot을 먼저 release하고 host owner lease를 마지막에
release하며 bind를 시작하지 않는다. Fixed routing ID와
automatic allocation을 같은 MeshNode에 설정하면 startup이 실패한다.

Lease option의 정확한 시그니처와 검증 관계는
[Location record §2](08-location-maintenance.ko.md#2-root-등록과-option)가 소유한다. Lease renew를 안전
기한까지 확인하지 못하면 host는 관련 MeshNode의 종료를 요청한다. 실행 중에 새 slot을 임의로 선택하지
않는다.
