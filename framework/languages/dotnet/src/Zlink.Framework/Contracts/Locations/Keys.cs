using System.Globalization;

namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Peer identity is the full five-component tuple. Null components are
/// compared as "no value" and still participate in the key.
/// </summary>
public readonly record struct ZLinkSpotLocationKey(
    string SpotId);

/// <summary>
/// Actor id is framework-wide unique. Actor type is creation, handler, and
/// diagnostic information, and does not participate in identity.
/// </summary>
public readonly record struct ZLinkActorLocationKey(
    string MeshName,
    string ActorId);


/// <summary>
/// Closed union of typed row keys. Watch events carry this model instead of
/// encoded string keys.
/// </summary>
public abstract record ZLinkLocationKey
{
    private ZLinkLocationKey() { }

    public sealed record MeshNode(ZLinkMeshNodeDescriptorKey Key) : ZLinkLocationKey;
    public sealed record Spot(ZLinkSpotLocationKey Key) : ZLinkLocationKey;
    public sealed record Actor(ZLinkActorLocationKey Key) : ZLinkLocationKey;
}

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
