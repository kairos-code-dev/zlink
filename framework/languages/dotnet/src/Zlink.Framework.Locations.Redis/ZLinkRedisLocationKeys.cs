using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Redis key schema for the official location store. Scripts receive prefixes
/// from this object when they must derive owner-dependent keys atomically.
/// </summary>
internal sealed class ZLinkRedisLocationKeys(string prefix)
{
    public RedisKey RowHashKey(string tag, string rowKey) => RowHashKeyPrefix(tag) + rowKey;

    public string RowHashKeyPrefix(string tag) => $"{prefix}:row:{tag}:";

    public RedisKey GenerationKey(string tag, string rowKey) => $"{prefix}:gen:{tag}:{rowKey}";

    public RedisKey KindIndexKey(string tag) => $"{prefix}:keys:{tag}";

    public string OwnerIndexKeyPrefix(string tag) => $"{prefix}:own:{tag}:";

    public RedisKey LeaseKey(string ownerId) => LeaseKeyPrefix() + ownerId;

    public string LeaseKeyPrefix() => $"{prefix}:lease:";

    public RedisKey LeaseIndexKey() => $"{prefix}:leases";

    public RedisKey RoutingIdAllocationGroupKey(string groupName) =>
        $"{prefix}:ridalloc:{groupName}";

    // Actor transfer authority keys (spec 41 §2/§3.1). actorRowKey is the
    // length-prefixed MeshName + Actor ID; the transfer HASH is per transfer id
    // and P:transfer-by-actor holds the single active transfer id per actor.
    public RedisKey TransferHashKey(string actorRowKey, string transferId) =>
        $"{prefix}:transfer:{actorRowKey}:{transferId}";

    public RedisKey TransferByActorKey(string actorRowKey) =>
        $"{prefix}:transfer-by-actor:{actorRowKey}";

    public string StampKey(string tag, string? meshName) =>
        meshName is null ? $"{prefix}:stamp:{tag}" : $"{prefix}:stamp:{tag}:{meshName}";

    public static string TagOf(ZLinkLocationKind kind) => kind switch
    {
        ZLinkLocationKind.Peer => ZLinkRedisLocationKinds.Peer.Tag,
        ZLinkLocationKind.Spot => ZLinkRedisLocationKinds.Spot.Tag,
        ZLinkLocationKind.Actor => ZLinkRedisLocationKinds.Actor.Tag,
        ZLinkLocationKind.Route => ZLinkRedisLocationKinds.Route.Tag,
        _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown location kind.")
    };
}
