namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Page request shared by store and operational list queries. The default
/// value means the configured default page size from the first page.
/// </summary>
public readonly record struct ZLinkPageRequest(
    int PageSize = 0,
    string? ContinuationToken = null);

public sealed record ZLinkLocationPage<T>(
    IReadOnlyList<T> Items,
    string? ContinuationToken);
