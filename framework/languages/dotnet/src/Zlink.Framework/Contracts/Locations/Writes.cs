namespace Zlink.Framework.Contracts.Locations;

public enum ZLinkLocationWriteIntent
{
    NewClaim = 1,
    Renew = 2,
    Takeover = 3
}

/// <summary>
/// Represents expected write races only. Store failures are reported as
/// exceptions, the same as read failures.
/// </summary>
public enum ZLinkLocationWriteStatus
{
    Stored = 1,
    IgnoredStale = 2,
    RejectedConflict = 3
}

/// <summary>
/// Result of a store write. Stored returns the store-issued row generation
/// and update time. Ownership fencing uses the separately claimed
/// <see cref="ZLinkLocationOwnerToken"/>.
/// </summary>
public sealed record ZLinkLocationWriteResult(
    ZLinkLocationWriteStatus Status,
    ulong Generation,
    DateTimeOffset UpdatedAt)
{
    public static ZLinkLocationWriteResult IgnoredStale { get; } =
        new(ZLinkLocationWriteStatus.IgnoredStale, 0, default);

    public static ZLinkLocationWriteResult RejectedConflict { get; } =
        new(ZLinkLocationWriteStatus.RejectedConflict, 0, default);

    public static ZLinkLocationWriteResult Stored(ulong generation, DateTimeOffset updatedAt) =>
        new(ZLinkLocationWriteStatus.Stored, generation, updatedAt);
}

public readonly record struct ZLinkLocationOwnerToken(
    string OwnerId,
    long LeaseGeneration)
{
    public ZLinkLocationOwnerToken(string ownerId, ulong generation)
        : this(ownerId, checked((long)generation))
    {
    }

    public long Generation => LeaseGeneration;
}

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
