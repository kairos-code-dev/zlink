namespace Zlink.Framework.Contracts.Locations;

public enum ZLinkPlacementObjectKind
{
    Actor = 1,
    UserSpot = 2,
    InstanceSpot = 3
}

public readonly record struct ZLinkAuthorityKey(string Value);

public sealed record ZLinkAuthoritySnapshot(
    string StoreVersion,
    ReadOnlyMemory<byte> Payload,
    ulong ObjectGeneration,
    ulong AuthorityOwnerGeneration,
    string OwnerId,
    long OwnerLeaseGeneration,
    DateTimeOffset StoreNow);

public abstract record ZLinkAuthorityReadResult
{
    private protected ZLinkAuthorityReadResult()
    {
    }

    public sealed record Missing(DateTimeOffset StoreNow) : ZLinkAuthorityReadResult;

    public sealed record Found(ZLinkAuthoritySnapshot Snapshot) : ZLinkAuthorityReadResult;
}

public abstract record ZLinkAuthorityExpectation
{
    private protected ZLinkAuthorityExpectation()
    {
    }

    public sealed record Missing : ZLinkAuthorityExpectation;

    public sealed record Found(string StoreVersion) : ZLinkAuthorityExpectation;
}

public sealed record ZLinkAuthorityEntry(
    ZLinkAuthorityKey Key,
    ZLinkAuthoritySnapshot Snapshot);

public readonly record struct ZLinkAuthorityScanCursor
{
    public ZLinkAuthorityScanCursor(string encoded)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(encoded);
        var size = System.Text.Encoding.UTF8.GetByteCount(encoded);
        if (size is < 1 or > 4096)
            throw new ArgumentOutOfRangeException(
                nameof(encoded),
                "Authority scan cursors must be 1 to 4096 UTF-8 bytes.");
        Encoded = encoded;
    }

    public string Encoded { get; }
}

public sealed record ZLinkAuthorityPage(
    IReadOnlyList<ZLinkAuthorityEntry> Items,
    ZLinkAuthorityScanCursor? NextCursor);

public abstract record ZLinkAuthorityScanResult
{
    private protected ZLinkAuthorityScanResult()
    {
    }

    public sealed record Page(ZLinkAuthorityPage Value) : ZLinkAuthorityScanResult;

    public sealed record ScanExpired : ZLinkAuthorityScanResult;
}

public abstract record ZLinkAuthorityMutation
{
    private protected ZLinkAuthorityMutation()
    {
    }

    public sealed record Put(
        ReadOnlyMemory<byte> Payload,
        ZLinkAuthorityGenerationTransition GenerationTransition,
        ZLinkLocationOwnerToken? TargetOwner,
        ZLinkRelocationCapacityFence? RelocationCapacityFence)
        : ZLinkAuthorityMutation;

    public sealed record Delete : ZLinkAuthorityMutation;
}

public enum ZLinkAuthorityGenerationTransition
{
    Preserve = 1,
    NewOwner = 2,
    NewObject = 3
}

public readonly record struct ZLinkRelocationCapacityFence(string Value);

public abstract record ZLinkAuthorityCompareExchangeResult
{
    private protected ZLinkAuthorityCompareExchangeResult()
    {
    }

    public sealed record Stored(ZLinkAuthoritySnapshot Snapshot)
        : ZLinkAuthorityCompareExchangeResult;

    public sealed record Deleted(string StoreVersion, DateTimeOffset StoreNow)
        : ZLinkAuthorityCompareExchangeResult;

    public sealed record Conflict(ZLinkAuthorityReadResult Current)
        : ZLinkAuthorityCompareExchangeResult;

    public sealed record GenerationExhausted : ZLinkAuthorityCompareExchangeResult;
}

public sealed record ZLinkObjectReservationRequest(
    ZLinkPlacementObjectKind ObjectKind,
    ZLinkAuthorityKey Key,
    string StableType,
    string? PlacementProfile,
    string? AffinityKey,
    string CreationIntentReference,
    ReadOnlyMemory<byte> CreationIntentHash,
    int CreationIntentEncodedSize,
    ZLinkMeshNodeDescriptorKey TargetDescriptor,
    ZLinkLocationOwnerToken TargetOwner,
    ReadOnlyMemory<byte> CreatingPayload,
    int PendingCapacityDelta);

public sealed record ZLinkObjectReservation(
    ZLinkAuthorityKey Key,
    string StoreVersion,
    ulong ObjectGeneration,
    ulong AuthorityOwnerGeneration,
    string ReservationVersion,
    ZLinkMeshNodeDescriptorKey TargetDescriptor,
    ZLinkLocationOwnerToken TargetOwner);

public abstract record ZLinkObjectReserveResult
{
    private protected ZLinkObjectReserveResult()
    {
    }

    public sealed record Reserved(ZLinkObjectReservation Reservation)
        : ZLinkObjectReserveResult;

    public sealed record Conflict(ZLinkAuthorityReadResult Current)
        : ZLinkObjectReserveResult;

    public sealed record AlreadyExists(ZLinkAuthoritySnapshot Current)
        : ZLinkObjectReserveResult;

    public sealed record TypeMismatch(ZLinkAuthoritySnapshot Current)
        : ZLinkObjectReserveResult;

    public sealed record PlacementCapacityExhausted : ZLinkObjectReserveResult;

    public sealed record GenerationExhausted : ZLinkObjectReserveResult;
}

public abstract record ZLinkObjectCommitResult
{
    private protected ZLinkObjectCommitResult()
    {
    }

    public sealed record Committed(ZLinkAuthoritySnapshot Snapshot)
        : ZLinkObjectCommitResult;

    public sealed record AlreadyCommitted(ZLinkAuthoritySnapshot Snapshot)
        : ZLinkObjectCommitResult;

    public sealed record Stale : ZLinkObjectCommitResult;

    public sealed record GenerationExhausted : ZLinkObjectCommitResult;
}

public abstract record ZLinkObjectAbortResult
{
    private protected ZLinkObjectAbortResult()
    {
    }

    public sealed record Aborted : ZLinkObjectAbortResult;

    public sealed record AlreadyAborted : ZLinkObjectAbortResult;

    public sealed record Stale : ZLinkObjectAbortResult;

    public sealed record GenerationExhausted : ZLinkObjectAbortResult;
}

public sealed record ZLinkRelocationCapacityReservationRequest(
    Guid ReservationId,
    ZLinkAuthorityKey Key,
    string ExpectedStoreVersion,
    ZLinkPlacementObjectKind ObjectKind,
    string StableType,
    ZLinkMeshNodeDescriptorKey SourceDescriptor,
    ZLinkLocationOwnerToken SourceOwner,
    ZLinkMeshNodeDescriptorKey TargetDescriptor,
    ZLinkLocationOwnerToken TargetOwner,
    int CapacityDelta);

public abstract record ZLinkRelocationCapacityReserveResult
{
    private protected ZLinkRelocationCapacityReserveResult() { }

    public sealed record Reserved(ZLinkRelocationCapacityFence Fence)
        : ZLinkRelocationCapacityReserveResult;

    public sealed record AlreadyReserved(ZLinkRelocationCapacityFence Fence)
        : ZLinkRelocationCapacityReserveResult;

    public sealed record Conflict(ZLinkAuthorityReadResult Current)
        : ZLinkRelocationCapacityReserveResult;

    public sealed record TargetUnavailable
        : ZLinkRelocationCapacityReserveResult;

    public sealed record PlacementCapacityExhausted
        : ZLinkRelocationCapacityReserveResult;
}

public enum ZLinkRelocationCapacityAbortResult
{
    Aborted = 1,
    AlreadyAborted = 2,
    AlreadyCommitted = 3,
    Stale = 4
}

public sealed record ZLinkAggregateParticipant(
    ZLinkAuthorityKey Key,
    string ExpectedStoreVersion,
    ZLinkAuthorityGenerationTransition OwnerTransition,
    ReadOnlyMemory<byte> AuthorityPayload,
    ReadOnlyMemory<byte> MembershipMutation);

public sealed record ZLinkAggregatePrepareRequest(
    Guid AggregateId,
    ulong AggregateGeneration,
    IReadOnlyList<ZLinkAggregateParticipant> Participants,
    ReadOnlyMemory<byte> InventoryDigest,
    IReadOnlyList<ZLinkRelocationCapacityFence> TargetReservations,
    ZLinkLocationOwnerToken TargetOwner);

public readonly record struct ZLinkAggregateFence(
    Guid AggregateId,
    ulong AggregateGeneration);

public abstract record ZLinkAggregatePrepareResult
{
    private protected ZLinkAggregatePrepareResult()
    {
    }

    public sealed record Prepared(ZLinkAggregateFence Fence)
        : ZLinkAggregatePrepareResult;

    public sealed record AlreadyPrepared(ZLinkAggregateFence Fence)
        : ZLinkAggregatePrepareResult;

    public sealed record Conflict : ZLinkAggregatePrepareResult;

    public sealed record Stale : ZLinkAggregatePrepareResult;

    public sealed record GenerationExhausted : ZLinkAggregatePrepareResult;
}

public enum ZLinkAggregateCommitResult
{
    Committed = 1,
    AlreadyCommitted = 2,
    Stale = 3,
    GenerationExhausted = 4
}

public enum ZLinkAggregateAbortResult
{
    Aborted = 1,
    AlreadyAborted = 2,
    Stale = 3
}

public interface IZLinkAuthorityStore
{
    ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkAuthorityCompareExchangeResult> CompareExchangeAuthorityAsync(
        ZLinkAuthorityKey key,
        ZLinkAuthorityExpectation expectation,
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

    ValueTask<ZLinkObjectAbortResult> AbortAsync(
        ZLinkObjectReservation reservation,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRelocationCapacityReserveResult> ReserveRelocationCapacityAsync(
        ZLinkRelocationCapacityReservationRequest request,
        CancellationToken cancellationToken = default) =>
        throw new NotSupportedException();

    ValueTask<ZLinkRelocationCapacityAbortResult> AbortRelocationCapacityAsync(
        ZLinkRelocationCapacityFence fence,
        CancellationToken cancellationToken = default) =>
        throw new NotSupportedException();

    ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
        ZLinkAggregatePrepareRequest request,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default);
}

public sealed record ZLinkRelocationStored(
    string Reference,
    uint ChecksumCrc32c,
    DateTimeOffset ExpiresAt,
    DateTimeOffset StoreNow);

public abstract record ZLinkRelocationReadResult
{
    private protected ZLinkRelocationReadResult()
    {
    }

    public sealed record Found(ReadOnlyMemory<byte> Payload) : ZLinkRelocationReadResult;

    public sealed record Missing : ZLinkRelocationReadResult;
}

public enum ZLinkRelocationDeleteResult
{
    Deleted = 0,
    Missing = 1
}

public abstract record ZLinkRelocationRenewResult
{
    private protected ZLinkRelocationRenewResult()
    {
    }

    public sealed record Renewed(DateTimeOffset ExpiresAt, DateTimeOffset StoreNow)
        : ZLinkRelocationRenewResult;

    public sealed record Missing : ZLinkRelocationRenewResult;
}

public interface IZLinkRelocationStore
{
    ValueTask<ZLinkRelocationStored> PutRelocationAsync(
        ReadOnlyMemory<byte> payload,
        TimeSpan retention,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRelocationReadResult> GetRelocationAsync(
        string reference,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRelocationRenewResult> RenewRelocationAsync(
        string reference,
        TimeSpan retention,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRelocationDeleteResult> DeleteRelocationAsync(
        string reference,
        CancellationToken cancellationToken = default);
}
