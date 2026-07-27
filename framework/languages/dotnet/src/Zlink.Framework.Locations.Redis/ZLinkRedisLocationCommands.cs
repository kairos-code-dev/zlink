using StackExchange.Redis;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Executes Redis scripts and owns their KEYS/ARGV order. The public store
/// chooses the contract operation; this class translates that operation into
/// the script protocol.
/// </summary>
internal sealed class ZLinkRedisLocationCommands(ZLinkRedisLocationKeys keys)
{
    public async ValueTask<ZLinkLocationWriteResult> WriteAsync<TRow>(
        IDatabase database,
        ZLinkRedisLocationKind<TRow> kind,
        TRow row,
        ZLinkLocationWriteIntent intent,
        string? spotId = null)
        where TRow : class
    {
        var intentName = intent switch
        {
            ZLinkLocationWriteIntent.NewClaim => "new",
            ZLinkLocationWriteIntent.Renew => "renew",
            ZLinkLocationWriteIntent.Takeover => "takeover",
            _ => throw new ArgumentOutOfRangeException(nameof(intent), intent, "Unknown write intent.")
        };
        var rowKey = kind.EncodeKey(row);
        var meshName = kind.MeshOf(row);
        var json = ZLinkRedisLocationRowJson.Serialize(row);
        var rowHashKey = keys.RowHashKey(kind.Tag, rowKey);
        while (true)
        {
            var expectedOwner = await database.HashGetAsync(rowHashKey, "owner")
                .ConfigureAwait(false);
            var ownerForLeaseKey = expectedOwner.IsNull
                ? kind.OwnerOf(row)
                : expectedOwner.ToString();
            var result = (RedisResult[])(await database.ScriptEvaluateAsync(
                ZLinkRedisLocationScripts.Write,
                [
                    rowHashKey,
                    keys.GenerationKey(kind.Tag, rowKey),
                    keys.KindIndexKey(kind.Tag),
                    keys.HybridOwnerLeaseKey(ownerForLeaseKey),
                    spotId is null
                        ? keys.HybridSchemaKey()
                        : keys.HybridEntrySpotIdClaimKey(spotId)
                ],
                [
                    intentName,
                    kind.OwnerOf(row),
                    kind.GenerationOf(row),
                    json,
                    rowKey,
                    keys.OwnerIndexKeyPrefix(kind.Tag),
                    keys.StampKey(kind.Tag, meshName),
                    meshName is null ? string.Empty : keys.StampKey(kind.Tag, meshName: null),
                    meshName is null ? "0" : "1",
                    meshName ?? string.Empty,
                    expectedOwner.IsNull ? string.Empty : expectedOwner,
                    spotId is null ? "0" : "1"
                ]).ConfigureAwait(false))!;
            if ((string)result[0]! != "retry")
                return ToWriteResult(result);
        }
    }

    public async ValueTask<ZLinkLocationWriteResult> WriteMeshNodeAsync(
        IDatabase database,
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent)
    {
        var intentName = intent switch
        {
            ZLinkLocationWriteIntent.NewClaim => "new",
            ZLinkLocationWriteIntent.Renew => "renew",
            ZLinkLocationWriteIntent.Takeover => "takeover",
            _ => throw new ArgumentOutOfRangeException(
                nameof(intent),
                intent,
                "Unknown write intent.")
        };
        var kind = ZLinkRedisLocationKinds.MeshNode;
        var rowKey = kind.EncodeKey(descriptor);
        var json = ZLinkRedisLocationRowJson.Serialize(descriptor);
        var ownerToken = OwnerTokenDigest(
            descriptor.OwnerId,
            descriptor.LeaseGeneration);
        var descriptorIndex = DescriptorIndexKey(rowKey);
        var ownerIndex = DescriptorOwnerIndexKey(rowKey, ownerToken);
        var immutableDigest = ImmutableDescriptorDigest(descriptor);
        var rowHashKey = keys.RowHashKey(kind.Tag, rowKey);
        var entrySpotId = descriptor.EntrySpotId ?? string.Empty;
        while (true)
        {
            var currentValues = await database.HashGetAsync(
                    rowHashKey,
                    ["owner", "json"])
                .ConfigureAwait(false);
            var expectedOwner = currentValues[0].IsNull
                ? string.Empty
                : currentValues[0].ToString();
            var currentLeaseKey = keys.HybridOwnerLeaseKey(
                string.IsNullOrEmpty(expectedOwner)
                    ? descriptor.OwnerId
                    : expectedOwner);
            var oldOwnerIndex = ownerIndex;
            var oldEntrySpotId = entrySpotId;
            ulong oldLifecycleGeneration = descriptor.LifecycleGeneration;
            if (!currentValues[1].IsNull)
            {
                var current = ZLinkRedisLocationRowJson
                    .Deserialize<ZLinkMeshNodeDescriptor>(
                        currentValues[1].ToString());
                oldOwnerIndex = DescriptorOwnerIndexKey(
                    rowKey,
                    OwnerTokenDigest(
                        current.OwnerId,
                        current.LeaseGeneration));
                oldEntrySpotId = current.EntrySpotId ?? string.Empty;
                oldLifecycleGeneration = current.LifecycleGeneration;
            }
            var result = (RedisResult[])(await database.ScriptEvaluateAsync(
                ZLinkRedisLocationScripts.WriteMeshNode,
                [
                    rowHashKey,
                    keys.GenerationKey(kind.Tag, rowKey),
                    keys.KindIndexKey(kind.Tag),
                    keys.HybridOwnerLeaseKey(descriptor.OwnerId),
                    keys.HybridDescriptorKey(rowKey),
                    keys.HybridDescriptorAdmissionKey(rowKey),
                    descriptorIndex,
                    ownerIndex,
                    currentLeaseKey,
                    oldOwnerIndex,
                    keys.HybridEntrySpotIdClaimKey(
                        entrySpotId.Length == 0
                            ? "__none__"
                            : entrySpotId),
                    keys.HybridEntrySpotIdClaimKey(
                        oldEntrySpotId.Length == 0
                            ? "__none__"
                            : oldEntrySpotId),
                    keys.HybridEntrySpotIdClaimIndexKey(),
                    keys.RowHashKey(
                        ZLinkRedisLocationKinds.Spot.Tag,
                        entrySpotId.Length == 0
                            ? "__none__"
                            : ZLinkRedisLocationKeyCodec.EncodeSpotKey(
                                new ZLinkSpotLocationKey(entrySpotId))),
                    keys.HybridAuthorityCurrentKey(
                        entrySpotId.Length == 0
                            ? "__none__"
                            : Zlink.Framework.Runtime.Spots
                                .ZLinkUserSpotAuthorityPayloadCodec
                                .AuthorityKey(entrySpotId).Value)
                ],
                [
                    intentName,
                    descriptor.OwnerId,
                    descriptor.LeaseGeneration,
                    json,
                    rowKey,
                    keys.OwnerIndexKeyPrefix(kind.Tag),
                    keys.StampKey(kind.Tag, descriptor.MeshName),
                    keys.StampKey(kind.Tag, meshName: null),
                    "1",
                    descriptor.MeshName,
                    descriptor.DescriptorRevision,
                    descriptor.LifecycleGeneration,
                    (int)descriptor.ObjectRole,
                    descriptor.PlacementWeight <= 0
                        ? 0
                        : (int)descriptor.State,
                    descriptor.ApplicationVersion,
                    JsonSerializer.Serialize(descriptor.ObjectCapabilities),
                    descriptor.Capacity.Actors.Limit,
                    descriptor.Capacity.Spots.Limit,
                    descriptor.ActivationConcurrency.Limit,
                    descriptor.EntrySpotId ?? string.Empty,
                    immutableDigest,
                    expectedOwner,
                    oldLifecycleGeneration
                ]).ConfigureAwait(false))!;
            if ((string)result[0]! != "retry")
                return ToWriteResult(result);
        }
    }

    public async ValueTask<ZLinkLocationWriteResult> RemoveAsync(
        IDatabase database,
        string tag,
        string rowKey,
        string? meshName,
        ZLinkLocationOwnerToken owner)
    {
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.Remove,
            [keys.RowHashKey(tag, rowKey), keys.KindIndexKey(tag)],
            [
                owner.OwnerId,
                owner.Generation,
                rowKey,
                keys.OwnerIndexKeyPrefix(tag),
                keys.StampKey(tag, meshName),
                meshName is null ? string.Empty : keys.StampKey(tag, meshName: null)
            ]).ConfigureAwait(false))!;
        return ToWriteResult(result);
    }

    public async ValueTask<ZLinkLocationWriteResult> WriteClientServerAsync(
        IDatabase database,
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkLocationWriteIntent intent)
    {
        var intentName = intent switch
        {
            ZLinkLocationWriteIntent.NewClaim => "new",
            ZLinkLocationWriteIntent.Renew => "renew",
            ZLinkLocationWriteIntent.Takeover => "takeover",
            _ => throw new ArgumentOutOfRangeException(nameof(intent))
        };
        var kind = ZLinkRedisLocationKinds.ClientServer;
        var rowKey = kind.EncodeKey(descriptor);
        var rowHashKey = keys.RowHashKey(kind.Tag, rowKey);
        var json = ZLinkRedisLocationRowJson.Serialize(descriptor);
        while (true)
        {
            var expectedOwner = await database.HashGetAsync(
                    rowHashKey,
                    "owner")
                .ConfigureAwait(false);
            var currentOwner = expectedOwner.IsNull
                ? descriptor.OwnerId
                : expectedOwner.ToString();
            var result = (RedisResult[])(await database.ScriptEvaluateAsync(
                ZLinkRedisLocationScripts.WriteClientServer,
                [
                    rowHashKey,
                    keys.GenerationKey(kind.Tag, rowKey),
                    keys.KindIndexKey(kind.Tag),
                    keys.HybridOwnerLeaseKey(descriptor.OwnerId),
                    keys.HybridOwnerLeaseKey(currentOwner),
                    keys.ClientServerChannelIndexKey(descriptor.ChannelName)
                ],
                [
                    intentName,
                    descriptor.OwnerId,
                    descriptor.LeaseGeneration,
                    json,
                    rowKey,
                    keys.OwnerIndexKeyPrefix(kind.Tag),
                    descriptor.ChannelName,
                    expectedOwner.IsNull ? string.Empty : expectedOwner
                ]).ConfigureAwait(false))!;
            if ((string)result[0]! != "retry")
                return ToWriteResult(result);
        }
    }

    public async ValueTask<ZLinkLocationWriteResult> RemoveClientServerAsync(
        IDatabase database,
        string rowKey,
        string channelName,
        ZLinkLocationOwnerToken owner)
    {
        var kind = ZLinkRedisLocationKinds.ClientServer;
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RemoveClientServer,
            [
                keys.RowHashKey(kind.Tag, rowKey),
                keys.KindIndexKey(kind.Tag),
                keys.ClientServerChannelIndexKey(channelName)
            ],
            [
                owner.OwnerId,
                owner.LeaseGeneration,
                rowKey,
                keys.OwnerIndexKeyPrefix(kind.Tag)
            ]).ConfigureAwait(false))!;
        return ToWriteResult(result);
    }

    public async ValueTask<ZLinkLocationWriteResult> WriteFanoutAsync(
        IDatabase database,
        ZLinkFanoutPublisherDescriptor descriptor,
        ZLinkLocationWriteIntent intent)
    {
        var intentName = intent switch
        {
            ZLinkLocationWriteIntent.NewClaim => "new",
            ZLinkLocationWriteIntent.Renew => "renew",
            ZLinkLocationWriteIntent.Takeover => "takeover",
            _ => throw new ArgumentOutOfRangeException(nameof(intent))
        };
        var kind = ZLinkRedisLocationKinds.Fanout;
        var rowKey = kind.EncodeKey(descriptor);
        var rowHashKey = keys.RowHashKey(kind.Tag, rowKey);
        var json = ZLinkRedisLocationRowJson.Serialize(descriptor);
        while (true)
        {
            var expectedOwner = await database.HashGetAsync(
                    rowHashKey,
                    "owner")
                .ConfigureAwait(false);
            var currentOwner = expectedOwner.IsNull
                ? descriptor.OwnerId
                : expectedOwner.ToString();
            var result = (RedisResult[])(await database.ScriptEvaluateAsync(
                ZLinkRedisLocationScripts.WriteClientServer,
                [
                    rowHashKey,
                    keys.GenerationKey(kind.Tag, rowKey),
                    keys.KindIndexKey(kind.Tag),
                    keys.HybridOwnerLeaseKey(descriptor.OwnerId),
                    keys.HybridOwnerLeaseKey(currentOwner),
                    keys.FanoutChannelIndexKey(descriptor.ChannelName)
                ],
                [
                    intentName,
                    descriptor.OwnerId,
                    descriptor.LeaseGeneration,
                    json,
                    rowKey,
                    keys.OwnerIndexKeyPrefix(kind.Tag),
                    descriptor.ChannelName,
                    expectedOwner.IsNull ? string.Empty : expectedOwner
                ]).ConfigureAwait(false))!;
            if ((string)result[0]! != "retry")
                return ToWriteResult(result);
        }
    }

    public async ValueTask<ZLinkLocationWriteResult> RemoveFanoutAsync(
        IDatabase database,
        string rowKey,
        string channelName,
        ZLinkLocationOwnerToken owner)
    {
        var kind = ZLinkRedisLocationKinds.Fanout;
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RemoveClientServer,
            [
                keys.RowHashKey(kind.Tag, rowKey),
                keys.KindIndexKey(kind.Tag),
                keys.FanoutChannelIndexKey(channelName)
            ],
            [
                owner.OwnerId,
                owner.LeaseGeneration,
                rowKey,
                keys.OwnerIndexKeyPrefix(kind.Tag)
            ]).ConfigureAwait(false))!;
        return ToWriteResult(result);
    }

    public async ValueTask<ZLinkLocationWriteResult> RemoveMeshNodeAsync(
        IDatabase database,
        string rowKey,
        string meshName,
        ZLinkLocationOwnerToken owner)
    {
        var rowHashKey = keys.RowHashKey(
            ZLinkRedisLocationKinds.MeshNode.Tag,
            rowKey);
        var json = await database.HashGetAsync(rowHashKey, "json")
            .ConfigureAwait(false);
        var ownerIndex = DescriptorOwnerIndexKey(
            rowKey,
            OwnerTokenDigest(owner.OwnerId, owner.LeaseGeneration));
        var entrySpotId = string.Empty;
        ulong lifecycleGeneration = 0;
        if (!json.IsNull)
        {
            var current = ZLinkRedisLocationRowJson
                .Deserialize<ZLinkMeshNodeDescriptor>(json.ToString());
            ownerIndex = DescriptorOwnerIndexKey(
                rowKey,
                OwnerTokenDigest(
                    current.OwnerId,
                    current.LeaseGeneration));
            entrySpotId = current.EntrySpotId ?? string.Empty;
            lifecycleGeneration = current.LifecycleGeneration;
        }
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RemoveMeshNode,
            [
                rowHashKey,
                keys.KindIndexKey(ZLinkRedisLocationKinds.MeshNode.Tag),
                keys.HybridDescriptorKey(rowKey),
                keys.HybridDescriptorAdmissionKey(rowKey),
                DescriptorIndexKey(rowKey),
                ownerIndex,
                keys.HybridEntrySpotIdClaimKey(
                    entrySpotId.Length == 0
                        ? "__none__"
                        : entrySpotId),
                keys.HybridEntrySpotIdClaimIndexKey()
            ],
            [
                owner.OwnerId,
                owner.Generation,
                rowKey,
                keys.OwnerIndexKeyPrefix(
                    ZLinkRedisLocationKinds.MeshNode.Tag),
                keys.StampKey(
                    ZLinkRedisLocationKinds.MeshNode.Tag,
                    meshName),
                keys.StampKey(
                    ZLinkRedisLocationKinds.MeshNode.Tag,
                    meshName: null),
                entrySpotId,
                lifecycleGeneration
            ]).ConfigureAwait(false))!;
        return ToWriteResult(result);
    }

    public async ValueTask<ZLinkOwnerLeaseClaimResult> ClaimOwnerLeaseAsync(
        IDatabase database,
        string ownerId,
        TimeSpan leaseTtl)
    {
        var ttlMs = Math.Max(1L, (long)leaseTtl.TotalMilliseconds);
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.ClaimLease,
            [
                keys.HybridOwnerLeaseKey(ownerId),
                keys.HybridOwnerLeaseIndexKey(),
                keys.HybridCounterKey()
            ],
            [ownerId, ttlMs]).ConfigureAwait(false))!;
        var status = (string)result[0]!;
        var now = DateTimeOffset.FromUnixTimeMilliseconds((long)result[1]);
        return status switch
        {
            "claimed" => new ZLinkOwnerLeaseClaimResult.Claimed(
                new ZLinkLocationOwnerToken(ownerId, (long)result[2]),
                now + TimeSpan.FromMilliseconds(ttlMs),
                now),
            "conflict" => new ZLinkOwnerLeaseClaimResult.Conflict(),
            "exhausted" => new ZLinkOwnerLeaseClaimResult.GenerationExhausted(),
            _ => throw new InvalidOperationException(
                $"Unknown owner lease claim result '{status}'.")
        };
    }

    public async ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
        IDatabase database,
        string ownerId)
    {
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.ReadLease,
            [keys.HybridOwnerLeaseKey(ownerId)],
            []).ConfigureAwait(false))!;
        if ((string)result[0]! == "missing")
            return new ZLinkOwnerLeaseReadResult.Missing();
        var now = DateTimeOffset.FromUnixTimeMilliseconds((long)result[1]);
        return new ZLinkOwnerLeaseReadResult.Found(
            new ZLinkLocationOwnerToken(ownerId, (long)result[2]),
            now + TimeSpan.FromMilliseconds((long)result[3]),
            now);
    }

    public async ValueTask<ZLinkOwnerLeaseRenewResult> RenewOwnerLeaseAsync(
        IDatabase database,
        ZLinkLocationOwnerToken token,
        TimeSpan leaseTtl)
    {
        var ttlMs = Math.Max(1L, (long)leaseTtl.TotalMilliseconds);
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RenewExactLease,
            [keys.HybridOwnerLeaseKey(token.OwnerId)],
            [token.LeaseGeneration, ttlMs]).ConfigureAwait(false))!;
        if ((string)result[0]! == "stale")
            return new ZLinkOwnerLeaseRenewResult.Stale();
        var now = DateTimeOffset.FromUnixTimeMilliseconds((long)result[1]);
        return new ZLinkOwnerLeaseRenewResult.Renewed(
            now + TimeSpan.FromMilliseconds(ttlMs),
            now);
    }

    public async ValueTask<ZLinkOwnerLeaseReleaseResult> ReleaseOwnerLeaseAsync(
        IDatabase database,
        ZLinkLocationOwnerToken token)
    {
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.ReleaseExactLease,
            [
                keys.HybridOwnerLeaseKey(token.OwnerId),
                keys.HybridOwnerLeaseIndexKey()
            ],
            [token.OwnerId, token.LeaseGeneration]).ConfigureAwait(false))!;
        return (string)result[0]! == "released"
            ? ZLinkOwnerLeaseReleaseResult.Released
            : ZLinkOwnerLeaseReleaseResult.Stale;
    }

    public async ValueTask<long> RemoveAllByOwnerAsync(
        IDatabase database,
        ZLinkLocationOwnerToken owner)
    {
        var ownerId = owner.OwnerId;
        var removed = await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RemoveAllByOwner,
            [
                (RedisKey)(keys.OwnerIndexKeyPrefix(ZLinkRedisLocationKinds.MeshNode.Tag) + ownerId),
                (RedisKey)(keys.OwnerIndexKeyPrefix(ZLinkRedisLocationKinds.Spot.Tag) + ownerId),
                (RedisKey)(keys.OwnerIndexKeyPrefix(ZLinkRedisLocationKinds.Actor.Tag) + ownerId),
                (RedisKey)(keys.OwnerIndexKeyPrefix(ZLinkRedisLocationKinds.ClientServer.Tag) + ownerId),
                (RedisKey)(keys.OwnerIndexKeyPrefix(ZLinkRedisLocationKinds.Fanout.Tag) + ownerId),
                keys.KindIndexKey(ZLinkRedisLocationKinds.MeshNode.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.Spot.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.Actor.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.ClientServer.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.Fanout.Tag),
                keys.HybridOwnerLeaseKey(ownerId),
                keys.HybridEntrySpotIdClaimIndexKey()
            ],
            [
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.MeshNode.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.Spot.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.Actor.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.ClientServer.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.Fanout.Tag),
                keys.StampKey(ZLinkRedisLocationKinds.MeshNode.Tag, meshName: null),
                keys.StampKey(ZLinkRedisLocationKinds.Spot.Tag, meshName: null),
                keys.StampKey(ZLinkRedisLocationKinds.Actor.Tag, meshName: null),
                string.Empty,
                string.Empty,
                owner.LeaseGeneration,
                ownerId
            ]).ConfigureAwait(false);
        return (long)removed;
    }

    public async ValueTask<ulong> GetMeshNodeChangeStampAsync(
        IDatabase database,
        string meshName)
    {
        var value = await database.StringGetAsync(
            keys.StampKey(ZLinkRedisLocationKinds.MeshNode.Tag, meshName))
            .ConfigureAwait(false);
        return value.IsNull ? 0 : (ulong)(long)value;
    }

    private RedisKey DescriptorIndexKey(string descriptorKey)
    {
        var physical = keys.HybridDescriptorKey(descriptorKey).ToString();
        return (RedisKey)(physical[..(physical.LastIndexOf(':') + 1)] + "index");
    }

    private RedisKey DescriptorOwnerIndexKey(
        string descriptorKey,
        string ownerTokenDigest)
    {
        var physical = keys.HybridDescriptorKey(descriptorKey).ToString();
        return (RedisKey)(
            physical[..(physical.LastIndexOf(':') + 1)]
            + "owner:"
            + ownerTokenDigest);
    }

    private static string OwnerTokenDigest(string ownerId, long generation) =>
        Convert.ToHexString(
            SHA256.HashData(
                Encoding.UTF8.GetBytes(
                    ownerId
                    + "\0"
                    + generation.ToString(
                        System.Globalization.CultureInfo.InvariantCulture))))
            .ToLowerInvariant();

    internal static string ImmutableDescriptorDigest(
        ZLinkMeshNodeDescriptor descriptor)
    {
        var segments = new List<string>
        {
            "zlink-mesh-node-immutable-v2",
            descriptor.MeshName,
            descriptor.Rid.ToHex(),
            descriptor.LifecycleGeneration.ToString(
                System.Globalization.CultureInfo.InvariantCulture),
            descriptor.Endpoint
        };
        var channelNames = descriptor.ChannelWeights.Keys
            .Order(Utf8StringComparer.Instance)
            .ToArray();
        segments.Add(channelNames.Length.ToString(
            System.Globalization.CultureInfo.InvariantCulture));
        segments.AddRange(channelNames);
        segments.Add(descriptor.SecurityIdentity);
        segments.Add(descriptor.ApplicationVersion.ToString(
            System.Globalization.CultureInfo.InvariantCulture));
        segments.Add(descriptor.ObjectRole switch
        {
            ZLinkMeshNodeObjectRole.None => "none",
            ZLinkMeshNodeObjectRole.Client => "client",
            ZLinkMeshNodeObjectRole.Server => "server",
            _ => throw new ArgumentOutOfRangeException(nameof(descriptor))
        });
        segments.Add(descriptor.EntrySpotId is null ? "0" : "1");
        if (descriptor.EntrySpotId is not null)
            segments.Add(descriptor.EntrySpotId);
        segments.Add(descriptor.Capacity.Actors.Limit.ToString(
            System.Globalization.CultureInfo.InvariantCulture));
        segments.Add(descriptor.Capacity.Spots.Limit.ToString(
            System.Globalization.CultureInfo.InvariantCulture));
        segments.Add(descriptor.ActivationConcurrency.Limit.ToString(
            System.Globalization.CultureInfo.InvariantCulture));
        var capabilities = descriptor.ObjectCapabilities
            .OrderBy(
                static capability => ObjectKindToken(capability.ObjectKind),
                Utf8StringComparer.Instance)
            .ThenBy(
                static capability => capability.StableType,
                Utf8StringComparer.Instance)
            .ToArray();
        segments.Add(capabilities.Length.ToString(
            System.Globalization.CultureInfo.InvariantCulture));
        foreach (var capability in capabilities)
        {
            segments.Add(ObjectKindToken(capability.ObjectKind));
            segments.Add(capability.StableType);
            segments.Add(capability.Policy switch
            {
                ZLinkObjectMaintenancePolicyKind.Disabled => "disabled",
                ZLinkObjectMaintenancePolicyKind.Recreate => "recreate",
                ZLinkObjectMaintenancePolicyKind.Snapshot => "snapshot",
                _ => throw new ArgumentOutOfRangeException(
                    nameof(descriptor))
            });
            segments.Add(capability.HasSnapshotAdapter ? "1" : "0");
            segments.Add(capability.ObjectKind == ZLinkPlacementObjectKind.Actor
                ? string.Empty
                : capability.Limit.ToString(
                    System.Globalization.CultureInfo.InvariantCulture));
        }
        var preimage = string.Concat(segments.Select(static segment =>
            Encoding.UTF8.GetByteCount(segment).ToString(
                System.Globalization.CultureInfo.InvariantCulture)
            + ":"
            + segment));
        return Convert.ToHexString(
            SHA256.HashData(Encoding.UTF8.GetBytes(preimage)))
            .ToLowerInvariant();
    }

    private static string ObjectKindToken(ZLinkPlacementObjectKind kind) =>
        kind switch
        {
            ZLinkPlacementObjectKind.Actor => "actor",
            ZLinkPlacementObjectKind.UserSpot => "user_spot",
            ZLinkPlacementObjectKind.InstanceSpot => "instance_spot",
            _ => throw new ArgumentOutOfRangeException(nameof(kind))
        };

    private sealed class Utf8StringComparer : IComparer<string>
    {
        internal static Utf8StringComparer Instance { get; } = new();

        public int Compare(string? left, string? right)
        {
            if (ReferenceEquals(left, right))
                return 0;
            if (left is null)
                return -1;
            if (right is null)
                return 1;
            return Encoding.UTF8.GetBytes(left).AsSpan().SequenceCompareTo(
                Encoding.UTF8.GetBytes(right));
        }
    }

    private static ZLinkLocationWriteResult ToWriteResult(RedisResult[] result)
    {
        var status = (string)result[0]!;
        return status switch
        {
            "stored" => ZLinkLocationWriteResult.Stored(
                (ulong)(long)result[1],
                DateTimeOffset.FromUnixTimeMilliseconds((long)result[2])),
            "conflict" => ZLinkLocationWriteResult.RejectedConflict,
            _ => ZLinkLocationWriteResult.IgnoredStale
        };
    }
}
