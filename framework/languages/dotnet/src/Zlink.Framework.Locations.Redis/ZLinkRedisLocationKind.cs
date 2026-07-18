namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Per-kind row contract for the Redis store. It keeps canonical key encoding,
/// mesh scope selection, owner token access, and load-time store-field
/// application together so command code does not need row-specific switches.
/// The row JSON is stored exactly as the writer serialized it; only the
/// store-clock UpdatedAt overwrites a row field on load (spec 41 §2).
/// </summary>
internal sealed class ZLinkRedisLocationKind<TRow>
    where TRow : class
{
    public required string Tag { get; init; }

    public required Func<TRow, string> EncodeKey { get; init; }

    public required Func<TRow, string?> MeshOf { get; init; }

    public required Func<TRow, string> OwnerOf { get; init; }

    /// <summary>Row-carried fencing value used as the renew guard against
    /// the hash's store generation; 0 skips the guard for rows that do not
    /// carry the store token (actor rows fence through the transfer
    /// authority instead).</summary>
    public required Func<TRow, ulong> GenerationOf { get; init; }

    public required Func<TRow, DateTimeOffset, TRow> Finalize { get; init; }
}

internal static class ZLinkRedisLocationKinds
{
    internal static readonly ZLinkRedisLocationKind<ZLinkMeshNodeDescriptor> MeshNode = new()
    {
        Tag = "mesh",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
            new ZLinkMeshNodeDescriptorKey(row.MeshName, row.Rid)),
        MeshOf = static row => row.MeshName,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static row => row.LifecycleGeneration,
        Finalize = static (row, updatedAt) => row with { UpdatedAt = updatedAt }
    };

    internal static readonly ZLinkRedisLocationKind<ZLinkSpotLocation> Spot = new()
    {
        Tag = "spot",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodeSpotKey(
            new ZLinkSpotLocationKey(row.MeshName, row.SpotRid)),
        MeshOf = static row => row.MeshName,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static row => row.SpotGeneration,
        Finalize = static (row, updatedAt) => row with { UpdatedAt = updatedAt }
    };

    internal static readonly ZLinkRedisLocationKind<ZLinkActorLocation> Actor = new()
    {
        Tag = "actor",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodeActorKey(
            new ZLinkActorLocationKey(row.MeshName, row.ActorId)),
        MeshOf = static row => row.MeshName,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static _ => 0,
        Finalize = static (row, updatedAt) => row with { UpdatedAt = updatedAt }
    };
}
