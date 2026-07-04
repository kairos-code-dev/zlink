namespace Zlink.Framework.Contracts.Locations;

public sealed record ZLinkLocationWatchFilter(
    ZLinkLocationKind Kind,
    string? MeshName = null,
    ZLinkRouteKind? RouteKind = null);

public enum ZLinkLocationChangeType
{
    Upserted = 1,
    Removed = 2,
    Expired = 3
}

/// <summary>
/// Change events carry typed keys. Backends that transport encoded string
/// keys decode them inside the store implementation.
/// </summary>
public sealed record ZLinkLocationChanged(
    ZLinkLocationKind Kind,
    ZLinkLocationKey Key,
    ZLinkLocationChangeType ChangeType,
    long Generation,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkLocationChangeStampScope(
    ZLinkLocationKind Kind,
    string? MeshName);
