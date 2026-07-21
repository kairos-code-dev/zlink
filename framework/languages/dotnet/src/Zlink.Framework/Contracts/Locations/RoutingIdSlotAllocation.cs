namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Waits for the routing ids that the framework assigned to an allocation group. The provider
/// exposes completed assignments only; slot selection, renewal and release remain framework
/// responsibilities.
/// </summary>
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

/// <summary>
/// Optional location-store capability that atomically assigns routing-id slots. The same object
/// registered as <see cref="IZLinkLocationStore"/> implements this interface.
/// </summary>
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
    private protected ZLinkRoutingIdSlotAcquireResult()
    {
    }
}

public sealed record ZLinkRoutingIdSlotAcquired(ZLinkRoutingIdSlotAllocation Allocation)
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotGroupExhausted : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotGroupConfigurationMismatch(
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> ExpectedMembers,
    int ExpectedSlotCount,
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> ActualMembers,
    int ActualSlotCount)
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotIdentityModeConflict : ZLinkRoutingIdSlotAcquireResult;

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
    string MeshName,
    string RoutingIdPrefix);
