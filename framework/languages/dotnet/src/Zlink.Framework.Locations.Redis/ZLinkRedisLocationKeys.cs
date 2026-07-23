using StackExchange.Redis;
using System.Security.Cryptography;
using System.Text;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Redis key schema for the official location store. Scripts receive prefixes
/// from this object when they must derive owner-dependent keys atomically.
/// </summary>
internal sealed class ZLinkRedisLocationKeys
{
    private const string HybridHashTag = "{zlink-location-v1}";
    private readonly string prefix;

    public ZLinkRedisLocationKeys(string prefix)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(prefix);
        if (prefix.Contains('{', StringComparison.Ordinal)
            || prefix.Contains('}', StringComparison.Ordinal))
        {
            throw new ArgumentException(
                "Redis location key prefix must not contain '{' or '}'.",
                nameof(prefix));
        }
        this.prefix = prefix;
    }

    private string HybridBase => $"{prefix}:{HybridHashTag}";

    public RedisKey HybridSchemaKey() => $"{HybridBase}:schema";

    public RedisKey HybridCounterKey() => $"{HybridBase}:counter";

    public RedisKey HybridOwnerLeaseKey(string ownerId) =>
        $"{HybridBase}:owner-lease:{Digest(ownerId)}";

    public RedisKey HybridOwnerLeaseIndexKey() =>
        $"{HybridBase}:owner-leases";

    public RedisKey HybridDescriptorKey(string canonicalDescriptorKey) =>
        $"{HybridBase}:descriptor:mesh:{Digest(canonicalDescriptorKey)}";

    public RedisKey HybridDescriptorAdmissionKey(
        string canonicalDescriptorKey) =>
        $"{HybridBase}:descriptor-admission:mesh:{Digest(canonicalDescriptorKey)}";

    public RedisKey HybridAuthorityCurrentKey(string canonicalAuthorityKey) =>
        $"{HybridBase}:authority:current:{Digest(canonicalAuthorityKey)}";

    public RedisKey HybridAuthorityHistoryKey(string canonicalAuthorityKey) =>
        $"{HybridBase}:authority:history:{Digest(canonicalAuthorityKey)}";

    public RedisKey HybridAuthorityHistoryRevisionsKey(
        string canonicalAuthorityKey) =>
        $"{HybridBase}:authority:history-revisions:{Digest(canonicalAuthorityKey)}";

    public RedisKey HybridAuthorityKeyIndexKey() =>
        $"{HybridBase}:authority:key-index";

    public RedisKey HybridAuthorityIndexGcKey() =>
        $"{HybridBase}:authority:index-gc";

    public RedisKey HybridMembershipCurrentKey() =>
        $"{HybridBase}:membership:current";

    public RedisKey HybridMembershipHistoryKey(string canonicalAuthorityKey) =>
        $"{HybridBase}:membership:history:{Digest(canonicalAuthorityKey)}";

    public RedisKey HybridMembershipHistoryRevisionsKey(
        string canonicalAuthorityKey) =>
        $"{HybridBase}:membership:history-revisions:{Digest(canonicalAuthorityKey)}";

    public RedisKey HybridCapacityKey(bool type, bool pending) =>
        $"{HybridBase}:capacity:{(type ? "type" : "node")}:{(pending ? "pending" : "active")}";

    internal static string HybridCapacityNodeBucket(
        string canonicalDescriptorKey,
        ulong lifecycleGeneration)
    {
        var lifecycle = lifecycleGeneration.ToString(
            System.Globalization.CultureInfo.InvariantCulture);
        return Segment(canonicalDescriptorKey) + Segment(lifecycle);
    }

    internal static string HybridCapacityTypeBucket(
        string canonicalDescriptorKey,
        ulong lifecycleGeneration,
        ZLinkPlacementObjectKind objectKind,
        string stableType)
    {
        var token = objectKind switch
        {
            ZLinkPlacementObjectKind.Actor => "actor",
            ZLinkPlacementObjectKind.UserSpot => "user_spot",
            ZLinkPlacementObjectKind.InstanceSpot => "instance_spot",
            _ => throw new ArgumentOutOfRangeException(nameof(objectKind))
        };
        return HybridCapacityNodeBucket(
                canonicalDescriptorKey,
                lifecycleGeneration)
            + Segment(token)
            + Segment(stableType);
    }

    public RedisKey HybridCreationKey(string reservationId) =>
        $"{HybridBase}:creation:{NormalizeId(reservationId)}";

    public RedisKey HybridRelocationKey(string fenceId) =>
        $"{HybridBase}:relocation:{NormalizeId(fenceId)}";

    public RedisKey HybridAggregateKey(
        Guid aggregateId,
        ulong aggregateGeneration) =>
        $"{HybridBase}:aggregate:{aggregateId:N}:{aggregateGeneration}";

    public RedisKey HybridScanKey(string scanId) =>
        $"{HybridBase}:scan:{NormalizeId(scanId)}";

    public RedisKey HybridScanExpiryKey() =>
        $"{HybridBase}:scans:expiry";

    public RedisKey HybridScanWatermarkKey() =>
        $"{HybridBase}:scans:watermark";

    public static string CanonicalKeyHex(string value) =>
        Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();

    private static string Digest(string value) =>
        Convert.ToHexString(
                SHA256.HashData(Encoding.UTF8.GetBytes(value)))
            .ToLowerInvariant();

    private static string Segment(string value) =>
        Encoding.UTF8.GetByteCount(value).ToString(
            System.Globalization.CultureInfo.InvariantCulture)
        + ":"
        + value;

    private static string NormalizeId(string value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value);
        return value.Replace("-", string.Empty, StringComparison.Ordinal)
            .ToLowerInvariant();
    }

    public RedisKey RowHashKey(string tag, string rowKey) => RowHashKeyPrefix(tag) + rowKey;

    public string RowHashKeyPrefix(string tag) => $"{prefix}:row:{tag}:";

    public RedisKey GenerationKey(string tag, string rowKey) => $"{prefix}:gen:{tag}:{rowKey}";

    public RedisKey KindIndexKey(string tag) => $"{prefix}:keys:{tag}";

    public string OwnerIndexKeyPrefix(string tag) => $"{prefix}:own:{tag}:";

    public RedisKey ClientServerChannelIndexKey(string channelName) =>
        $"{prefix}:clientserver:channel:{Convert.ToHexString(Encoding.UTF8.GetBytes(channelName)).ToLowerInvariant()}";

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

    public static string TagOf(ZLinkLocationChangeScopeKind kind) => kind switch
    {
        ZLinkLocationChangeScopeKind.MeshNode => ZLinkRedisLocationKinds.MeshNode.Tag,
        ZLinkLocationChangeScopeKind.Spot => ZLinkRedisLocationKinds.Spot.Tag,
        ZLinkLocationChangeScopeKind.Actor => ZLinkRedisLocationKinds.Actor.Tag,
        ZLinkLocationChangeScopeKind.OwnerLease => "lease",
        ZLinkLocationChangeScopeKind.ActorTransfer => "transfer",
        _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown change stamp scope kind.")
    };
}
