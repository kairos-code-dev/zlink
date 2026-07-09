namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Per-kind row contract for the Redis store. It keeps canonical key encoding,
/// mesh scope selection, owner token access, and store-issued field application
/// together so command code does not need row-specific switches.
/// </summary>
internal sealed class ZLinkRedisLocationKind<TRow>
    where TRow : class
{
    public required string Tag { get; init; }

    public required Func<TRow, string> EncodeKey { get; init; }

    public required Func<TRow, string?> MeshOf { get; init; }

    public required Func<TRow, string> OwnerOf { get; init; }

    public required Func<TRow, long> GenerationOf { get; init; }

    public required Func<TRow, long, DateTimeOffset, TRow> Finalize { get; init; }
}

internal static class ZLinkRedisLocationKinds
{
    internal static readonly ZLinkRedisLocationKind<ZLinkPeerLocation> Peer = new()
    {
        Tag = "peer",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodePeerKey(new ZLinkPeerLocationKey(
            row.AutoConnectType, row.MeshName, row.Role, row.NodeRid, row.Endpoint)),
        MeshOf = static row => row.MeshName,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static row => row.Generation,
        Finalize = static (row, generation, updatedAt) =>
            row with { Generation = generation, UpdatedAt = updatedAt }
    };

    internal static readonly ZLinkRedisLocationKind<ZLinkSpotLocation> Spot = new()
    {
        Tag = "spot",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodeSpotKey(
            new ZLinkSpotLocationKey(row.MeshName, row.SpotRid)),
        MeshOf = static row => row.MeshName,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static row => row.Generation,
        Finalize = static (row, generation, updatedAt) =>
            row with { Generation = generation, UpdatedAt = updatedAt }
    };

    internal static readonly ZLinkRedisLocationKind<ZLinkActorLocation> Actor = new()
    {
        Tag = "actor",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodeActorKey(
            new ZLinkActorLocationKey(row.ActorId)),
        MeshOf = static _ => null,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static row => row.Generation,
        Finalize = static (row, generation, updatedAt) =>
            row with { Generation = generation, UpdatedAt = updatedAt }
    };

    internal static readonly ZLinkRedisLocationKind<ZLinkRouteLocation> Route = new()
    {
        Tag = "route",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodeRouteKey(
            new ZLinkRouteLocationKey(row.RouteKind, row.RouteKey)),
        MeshOf = static _ => null,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static row => row.Generation,
        Finalize = static (row, generation, updatedAt) =>
            row with { Generation = generation, UpdatedAt = updatedAt }
    };
}
