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
        ZLinkLocationWriteIntent intent)
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
                    keys.HybridOwnerLeaseKey(ownerForLeaseKey)
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
                    expectedOwner.IsNull ? string.Empty : expectedOwner
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
                    oldOwnerIndex
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
                    immutableDigest,
                    expectedOwner
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
        if (!json.IsNull)
        {
            var current = ZLinkRedisLocationRowJson
                .Deserialize<ZLinkMeshNodeDescriptor>(json.ToString());
            ownerIndex = DescriptorOwnerIndexKey(
                rowKey,
                OwnerTokenDigest(
                    current.OwnerId,
                    current.LeaseGeneration));
        }
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RemoveMeshNode,
            [
                rowHashKey,
                keys.KindIndexKey(ZLinkRedisLocationKinds.MeshNode.Tag),
                keys.HybridDescriptorKey(rowKey),
                keys.HybridDescriptorAdmissionKey(rowKey),
                DescriptorIndexKey(rowKey),
                ownerIndex
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
                    meshName: null)
            ]).ConfigureAwait(false))!;
        return ToWriteResult(result);
    }

    public async ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        IDatabase database,
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl)
    {
        // PX rejects non-positive values; a TTL that low means "already
        // expired", which one millisecond is close enough to.
        var ttlMs = Math.Max(1L, (long)leaseTtl.TotalMilliseconds);
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RenewLease,
            [
                keys.HybridOwnerLeaseKey(ownerId),
                keys.HybridOwnerLeaseIndexKey(),
                keys.HybridCounterKey(),
                OwnerLeaseMetadataKey()
            ],
            [ownerId, nodeRid.ToHex(), ttlMs]).ConfigureAwait(false))!;
        var nowMs = (long)result[0];
        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds(nowMs);
        return new ZLinkOwnerLeaseRenewal(storeNow + TimeSpan.FromMilliseconds(ttlMs), storeNow);
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
                keys.HybridCounterKey(),
                OwnerLeaseMetadataKey()
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
                keys.HybridOwnerLeaseIndexKey(),
                OwnerLeaseMetadataKey()
            ],
            [token.OwnerId, token.LeaseGeneration]).ConfigureAwait(false))!;
        return (string)result[0]! == "released"
            ? ZLinkOwnerLeaseReleaseResult.Released
            : ZLinkOwnerLeaseReleaseResult.Stale;
    }

    public async ValueTask<bool> RemoveOwnerLeaseAsync(IDatabase database, string ownerId)
    {
        var removed = (long)await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RemoveLease,
            [
                keys.HybridOwnerLeaseKey(ownerId),
                keys.HybridOwnerLeaseIndexKey(),
                OwnerLeaseMetadataKey()
            ],
            [ownerId]).ConfigureAwait(false);
        return removed != 0;
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
                keys.KindIndexKey(ZLinkRedisLocationKinds.MeshNode.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.Spot.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.Actor.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.ClientServer.Tag),
                keys.HybridOwnerLeaseKey(ownerId)
            ],
            [
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.MeshNode.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.Spot.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.Actor.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.ClientServer.Tag),
                keys.StampKey(ZLinkRedisLocationKinds.MeshNode.Tag, meshName: null),
                keys.StampKey(ZLinkRedisLocationKinds.Spot.Tag, meshName: null),
                keys.StampKey(ZLinkRedisLocationKinds.Actor.Tag, meshName: null),
                string.Empty,
                owner.LeaseGeneration
            ]).ConfigureAwait(false);
        return (long)removed;
    }

    public async ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(IDatabase database)
    {
        var owners = await database.SetMembersAsync(
                keys.HybridOwnerLeaseIndexKey())
            .ConfigureAwait(false);
        var scriptKeys = new RedisKey[owners.Length + 2];
        scriptKeys[0] = keys.HybridOwnerLeaseIndexKey();
        scriptKeys[1] = OwnerLeaseMetadataKey();
        var arguments = new RedisValue[owners.Length];
        for (var index = 0; index < owners.Length; index++)
        {
            var ownerId = owners[index].ToString();
            scriptKeys[index + 2] = keys.HybridOwnerLeaseKey(ownerId);
            arguments[index] = ownerId;
        }
        var raw = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.ListLeases,
            scriptKeys,
            arguments).ConfigureAwait(false))!;

        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds((long)raw[0]);
        var entries = (RedisResult[])raw[1]!;
        var leases = new List<ZLinkOwnerLease>(entries.Length / 3);
        for (var i = 0; i + 2 < entries.Length; i += 3)
        {
            var ownerId = (string)entries[i]!;
            var value = (string)entries[i + 1]!;
            var remainingMs = (long)entries[i + 2];

            // Compatibility metadata contains only the RoutingId and renewal
            // timestamp; the owner token and expiry authority remain in the
            // canonical three-field lease HASH.
            var first = value.IndexOf('|');
            var second = value.IndexOf('|', first + 1);
            var generation = long.Parse(value[..first]);
            var nodeRid = RoutingId.FromHex(value[(first + 1)..second]);
            var renewedAt = DateTimeOffset.FromUnixTimeMilliseconds(
                long.Parse(value[(second + 1)..]));
            leases.Add(new ZLinkOwnerLease(
                ownerId,
                nodeRid,
                storeNow + TimeSpan.FromMilliseconds(remainingMs),
                renewedAt)
            {
                LeaseGeneration = generation
            });
        }

        return new ZLinkOwnerLeaseSnapshot(leases, storeNow);
    }

    public async ValueTask<ulong> GetChangeStampAsync(
        IDatabase database,
        ZLinkLocationChangeStampScope scope)
    {
        var value = await database.StringGetAsync(
            keys.StampKey(ZLinkRedisLocationKeys.TagOf(scope.Kind), scope.MeshName)).ConfigureAwait(false);
        return value.IsNull ? 0 : (ulong)(long)value;
    }

    public async ValueTask<ZLinkRoutingIdSlotAcquireResult> AcquireRoutingIdSlotAsync(
        IDatabase database,
        ZLinkRoutingIdSlotAcquireRequest request)
    {
        var members = NormalizeMembers(request.Members);
        var config = JsonSerializer.Serialize(members);
        var ttlMs = Math.Max(1L, (long)request.LeaseTtl.TotalMilliseconds);
        var groupKey = keys.RoutingIdAllocationGroupKey(request.GroupName);
        var knownOwners = await ReadRoutingIdSlotOwnersAsync(
                database,
                groupKey)
            .ConfigureAwait(false);
        RedisValue[] arguments = new RedisValue[5 + knownOwners.Length];
        arguments[0] = config;
        arguments[1] = request.SlotCount;
        arguments[2] = request.OwnerId;
        arguments[3] = ttlMs;
        arguments[4] = string.Empty;
        var scriptKeys = new RedisKey[4 + knownOwners.Length];
        scriptKeys[0] = groupKey;
        scriptKeys[1] = keys.HybridOwnerLeaseKey(request.OwnerId);
        scriptKeys[2] = keys.HybridOwnerLeaseIndexKey();
        scriptKeys[3] = keys.HybridCounterKey();
        for (var index = 0; index < knownOwners.Length; index++)
        {
            scriptKeys[index + 4] = keys.HybridOwnerLeaseKey(
                knownOwners[index]);
            arguments[index + 5] = knownOwners[index];
        }
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.AcquireRoutingIdSlot,
            scriptKeys,
            arguments).ConfigureAwait(false))!;

        var status = (string)result[0]!;
        if (status == "exhausted") return new ZLinkRoutingIdSlotGroupExhausted();
        if (status == "lease-exhausted")
            throw new InvalidOperationException(
                "The provider owner lease generation counter is exhausted.");
        if (status == "identity-conflict") return new ZLinkRoutingIdSlotIdentityModeConflict();
        if (status == "mismatch")
        {
            var expectedMembers = JsonSerializer.Deserialize<ZLinkRoutingIdSlotAllocationMember[]>(
                                      (string)result[1]!)
                                  ?? [];
            return new ZLinkRoutingIdSlotGroupConfigurationMismatch(
                expectedMembers,
                (int)(long)result[2],
                members,
                request.SlotCount);
        }

        if (status != "acquired")
            throw new InvalidOperationException($"Unknown routing-id slot acquire result '{status}'.");

        var slot = (int)(long)result[1];
        var generation = (ulong)(long)result[2];
        var expiresAt = DateTimeOffset.FromUnixTimeMilliseconds((long)result[3]);
        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds((long)result[4]);
        return new ZLinkRoutingIdSlotAcquired(new ZLinkRoutingIdSlotAllocation(
            slot,
            new ZLinkLocationOwnerToken(
                request.OwnerId,
                checked((long)generation)),
            expiresAt,
            storeNow));
    }

    public async ValueTask<ZLinkRoutingIdSlotReleaseResult> ReleaseRoutingIdSlotAsync(
        IDatabase database,
        string groupName,
        int slot,
        ZLinkLocationOwnerToken owner)
    {
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.ReleaseRoutingIdSlot,
            [keys.RoutingIdAllocationGroupKey(groupName)],
            [slot, owner.OwnerId, owner.Generation]).ConfigureAwait(false))!;
        return (string)result[0]! == "released"
            ? ZLinkRoutingIdSlotReleaseResult.Released
            : ZLinkRoutingIdSlotReleaseResult.IgnoredStale;
    }

    public async ValueTask<ZLinkRoutingIdSlotAllocationSnapshot> ListRoutingIdSlotsAsync(
        IDatabase database,
        string groupName)
    {
        var groupKey = keys.RoutingIdAllocationGroupKey(groupName);
        var owners = await ReadRoutingIdSlotOwnersAsync(database, groupKey)
            .ConfigureAwait(false);
        var scriptKeys = new RedisKey[owners.Length + 1];
        var arguments = new RedisValue[owners.Length];
        scriptKeys[0] = groupKey;
        for (var index = 0; index < owners.Length; index++)
        {
            scriptKeys[index + 1] = keys.HybridOwnerLeaseKey(owners[index]);
            arguments[index] = owners[index];
        }
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.ListRoutingIdSlots,
            scriptKeys,
            arguments).ConfigureAwait(false))!;
        var config = (string)result[0]!;
        var members = string.IsNullOrEmpty(config)
            ? []
            : JsonSerializer.Deserialize<ZLinkRoutingIdSlotAllocationMember[]>(config) ?? [];
        var slotCount = (int)(long)result[1];
        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds((long)result[2]);
        var entries = (RedisResult[])result[3]!;
        var allocations = new List<ZLinkRoutingIdSlotAllocation>(entries.Length / 4);
        for (var index = 0; index + 3 < entries.Length; index += 4)
        {
            allocations.Add(new ZLinkRoutingIdSlotAllocation(
                (int)(long)entries[index],
                new ZLinkLocationOwnerToken(
                    (string)entries[index + 1]!,
                    (long)entries[index + 2]),
                DateTimeOffset.FromUnixTimeMilliseconds((long)entries[index + 3]),
                storeNow));
        }

        return new ZLinkRoutingIdSlotAllocationSnapshot(
            groupName,
            members,
            slotCount,
            allocations,
            storeNow);
    }

    private static IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> NormalizeMembers(
        IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> members) =>
        members.OrderBy(static member => member.MeshName, StringComparer.Ordinal)
            .ThenBy(static member => member.RoutingIdPrefix, StringComparer.Ordinal)
            .ToArray();

    private static async ValueTask<string[]> ReadRoutingIdSlotOwnersAsync(
        IDatabase database,
        RedisKey groupKey)
    {
        var fields = await database.HashKeysAsync(groupKey)
            .ConfigureAwait(false);
        return fields
            .Select(static field => field.ToString())
            .Where(static field => field.StartsWith(
                "owner:",
                StringComparison.Ordinal))
            .Select(static field => field["owner:".Length..])
            .ToArray();
    }

    private RedisKey OwnerLeaseMetadataKey() =>
        (RedisKey)(keys.HybridOwnerLeaseIndexKey().ToString() + ":metadata");

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
            "zlink-mesh-node-immutable-v1",
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
        segments.Add(descriptor.Capacity.Actors.Limit.ToString(
            System.Globalization.CultureInfo.InvariantCulture));
        segments.Add(descriptor.Capacity.Spots.Limit.ToString(
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
            segments.Add(capability.Limit.ToString(
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
