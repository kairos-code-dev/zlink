# .NET routing id 자동 할당 공개 계약

이 문서는 [공통 location runtime 계약](../../40-location-runtime.ko.md#12-routing-id-slot-allocation)을
`.NET`에서 표현하는 정확한 public interface를 고정한다.

## 1. builder

client/server channel, fanout channel, route mesh channel과 SpotNode builder는 각각 다음 세 메서드를
제공한다. 반환형은 호출한 builder interface 자신이다.

```csharp
Builder UseAllocatedRoutingId(int slotCount);
Builder UseAllocatedRoutingId(int slotCount, string routingIdPrefix);
Builder SetRoutingIdAllocationGroup(string groupName);
```

첫 번째 overload는 등록 이름을 prefix로 사용한다. 두 번째 overload는 생성할 routing id의 prefix만
바꾸며 기본 group 이름은 바꾸지 않는다. group 설정 호출은 자동 할당 호출 전후 어느 쪽에도 둘 수
있다. 고정 routing id 설정과 함께 사용하면 startup 전에 `ZLinkConfigurationException`이 발생한다.

## 2. allocation store capability

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

public abstract record ZLinkRoutingIdSlotAcquireResult
{
    // 결과 집합은 framework package 안에서만 확장할 수 있다.
    private protected ZLinkRoutingIdSlotAcquireResult();
}

public sealed record ZLinkRoutingIdSlotAcquired(
    ZLinkRoutingIdSlotAllocation Allocation) : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotGroupExhausted
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotGroupConfigurationMismatch(
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> ExpectedMembers,
    int ExpectedSlotCount,
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> ActualMembers,
    int ActualSlotCount) : ZLinkRoutingIdSlotAcquireResult;

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

public sealed record ZLinkRoutingIdSlotAllocationMember(
    string ChannelName,
    string RoutingIdPrefix);
```

`ZLinkRoutingIdSlotAcquireResult`의 닫힌 결과 집합은 `ZLinkRoutingIdSlotAcquired`,
`ZLinkRoutingIdSlotGroupExhausted`, `ZLinkRoutingIdSlotGroupConfigurationMismatch`,
`ZLinkRoutingIdSlotIdentityModeConflict`다. 성공 결과는 slot, owner token, lease 만료 시각과 같은 원자
연산에서 읽은 store 시각을 담는다. release 결과는 `Released` 또는 `IgnoredStale`다.

## 3. 준비된 결과 조회

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
```

결과는 group 이름, slot과 member 이름별 `RoutingId`를 제공한다. provider는 모든 socket bind와
location row 게시가 끝나 readiness에 도달한 뒤에만 완료된다. 등록되지 않은 group 이름은
`ZLinkConfigurationException`으로 실패한다.

## 4. location option

`ZLinkLocationOptions`는 기존 `HeartbeatInterval`, `OwnerLeaseTtl`과 함께
`RoutingIdFencingMargin`, `OwnerLeaseRenewTimeout`을 제공한다. 기본값은 각각 10초, 30초, 5초,
3초이며 공통 계약의 시간 관계를 만족해야 한다.

## 회귀 테스트

| 테스트 | 확인 기준 |
|--------|-----------|
| `NodesAndServicesTests.AddZLinkFramework_RegistersOneAllocatedRoutingIdCapabilityForAllBuilders` | 네 builder와 같은 store capability 등록 |
| `NodesAndServicesTests.HostStartup_AllocatesRoutingIdBeforeBindingAndPublishesReadyResult` | bind 전 할당과 readiness 이후 결과 조회 |
| `InMemoryLocationStoreTests.RoutingIdSlots_AssignLowestRecycleAndFenceStaleRelease` | 최소 slot, 재사용과 generation guard |
| `RedisLocationStoreTests.RoutingIdSlotAllocation_IsAtomicIdempotentAndFenced` | Redis 원자 할당, 멱등성과 stale release 차단 |
