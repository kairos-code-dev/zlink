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
/// Result of a store write. Stored returns the store-issued generation and
/// the store update time; callers keep them as the owner token.
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
    ulong Generation);

/// <summary>
/// Owner lease renewal result. Leases do not have generations, so they do
/// not reuse <see cref="ZLinkLocationWriteResult"/>.
/// </summary>
public sealed record ZLinkOwnerLeaseRenewal(
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset StoreNow);
