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

    /// <summary>Renew guard token compared against the hash's store
    /// generation; 0 makes the renew guard owner-id-only. Row content
    /// generations (lifecycle/spot) are writer-owned core values and are a
    /// different domain from the store's fencing generation (spec 41 §3.1),
    /// so no kind derives the guard from row content — removal presents the
    /// tracked store token explicitly instead.</summary>
    public required Func<TRow, ulong> GenerationOf { get; init; }

    public required Func<TRow, DateTimeOffset, ulong, TRow> Finalize { get; init; }
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
        GenerationOf = static _ => 0,
        Finalize = static (row, updatedAt, generation) => row with
        {
            UpdatedAt = updatedAt
        }
    };

    internal static readonly ZLinkRedisLocationKind<ZLinkSpotLocation> Spot = new()
    {
        Tag = "spot",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodeSpotKey(
            new ZLinkSpotLocationKey(row.SpotId)),
        MeshOf = static row => row.MeshName,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static _ => 0,
        Finalize = static (row, updatedAt, generation) => row with
        {
            UpdatedAt = updatedAt,
            AuthorityOwnerGeneration = generation
        }
    };

    internal static readonly ZLinkRedisLocationKind<ZLinkActorLocation> Actor = new()
    {
        Tag = "actor",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodeActorKey(
            new ZLinkActorLocationKey(row.MeshName, row.ActorId)),
        MeshOf = static row => row.MeshName,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static _ => 0,
        Finalize = static (row, updatedAt, generation) => row with
        {
            UpdatedAt = updatedAt,
            AuthorityOwnerGeneration = generation
        }
    };

    internal static readonly ZLinkRedisLocationKind<ZLinkClientServerServerDescriptor>
        ClientServer = new()
        {
            Tag = "clientserver",
            EncodeKey = static row =>
                ZLinkRedisLocationKeyCodec.EncodeClientServerKey(
                    new ZLinkClientServerServerDescriptorKey(
                        row.ChannelName,
                        row.ServerRid)),
            MeshOf = static _ => null,
            OwnerOf = static row => row.OwnerId,
            GenerationOf = static _ => 0,
            Finalize = static (row, updatedAt, _) => row with
            {
                UpdatedAt = updatedAt
            }
        };
}
