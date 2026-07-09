using StackExchange.Redis;

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
        var result = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.Write,
            [keys.RowHashKey(kind.Tag, rowKey), keys.GenerationKey(kind.Tag, rowKey), keys.KindIndexKey(kind.Tag)],
            [
                intentName,
                kind.OwnerOf(row),
                kind.GenerationOf(row),
                json,
                rowKey,
                keys.LeaseKeyPrefix(),
                keys.OwnerIndexKeyPrefix(kind.Tag),
                keys.StampKey(kind.Tag, meshName),
                meshName is null ? string.Empty : keys.StampKey(kind.Tag, meshName: null),
                meshName is null ? "0" : "1",
                meshName ?? string.Empty
            ]).ConfigureAwait(false))!;
        return ToWriteResult(result);
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

    public async ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        IDatabase database,
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl)
    {
        // PX rejects non-positive values; a TTL that low means "already
        // expired", which one millisecond is close enough to.
        var ttlMs = Math.Max(1L, (long)leaseTtl.TotalMilliseconds);
        var nowMs = (long)await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RenewLease,
            [keys.LeaseKey(ownerId), keys.LeaseIndexKey()],
            [ownerId, nodeRid.ToHex(), ttlMs]).ConfigureAwait(false);
        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds(nowMs);
        return new ZLinkOwnerLeaseRenewal(storeNow + TimeSpan.FromMilliseconds(ttlMs), storeNow);
    }

    public async ValueTask<bool> RemoveOwnerLeaseAsync(IDatabase database, string ownerId)
    {
        var removed = (long)await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RemoveLease,
            [keys.LeaseKey(ownerId), keys.LeaseIndexKey()],
            [ownerId]).ConfigureAwait(false);
        return removed != 0;
    }

    public async ValueTask<long> RemoveAllByOwnerAsync(IDatabase database, string ownerId)
    {
        var removed = await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RemoveAllByOwner,
            [
                (RedisKey)(keys.OwnerIndexKeyPrefix(ZLinkRedisLocationKinds.Peer.Tag) + ownerId),
                (RedisKey)(keys.OwnerIndexKeyPrefix(ZLinkRedisLocationKinds.Spot.Tag) + ownerId),
                (RedisKey)(keys.OwnerIndexKeyPrefix(ZLinkRedisLocationKinds.Actor.Tag) + ownerId),
                (RedisKey)(keys.OwnerIndexKeyPrefix(ZLinkRedisLocationKinds.Route.Tag) + ownerId),
                keys.KindIndexKey(ZLinkRedisLocationKinds.Peer.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.Spot.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.Actor.Tag),
                keys.KindIndexKey(ZLinkRedisLocationKinds.Route.Tag)
            ],
            [
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.Peer.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.Spot.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.Actor.Tag),
                keys.RowHashKeyPrefix(ZLinkRedisLocationKinds.Route.Tag),
                keys.StampKey(ZLinkRedisLocationKinds.Peer.Tag, meshName: null),
                keys.StampKey(ZLinkRedisLocationKinds.Spot.Tag, meshName: null),
                keys.StampKey(ZLinkRedisLocationKinds.Actor.Tag, meshName: null),
                keys.StampKey(ZLinkRedisLocationKinds.Route.Tag, meshName: null)
            ]).ConfigureAwait(false);
        return (long)removed;
    }

    public async ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(IDatabase database)
    {
        var raw = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.ListLeases,
            [keys.LeaseIndexKey()],
            [keys.LeaseKeyPrefix()]).ConfigureAwait(false))!;

        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds((long)raw[0]);
        var entries = (RedisResult[])raw[1]!;
        var leases = new List<ZLinkOwnerLease>(entries.Length / 3);
        for (var i = 0; i + 2 < entries.Length; i += 3)
        {
            var ownerId = (string)entries[i]!;
            var value = (string)entries[i + 1]!;
            var remainingMs = (long)entries[i + 2];

            // Lease values are "nodeRidHex|renewedAtMs"; expiry is computed
            // from the store clock plus the remaining Redis TTL, never from
            // an application wall clock.
            var separator = value.IndexOf('|');
            var nodeRid = RoutingId.FromHex(value[..separator]);
            var renewedAt = DateTimeOffset.FromUnixTimeMilliseconds(long.Parse(value[(separator + 1)..]));
            leases.Add(new ZLinkOwnerLease(
                ownerId,
                nodeRid,
                storeNow + TimeSpan.FromMilliseconds(remainingMs),
                renewedAt));
        }

        return new ZLinkOwnerLeaseSnapshot(leases, storeNow);
    }

    public async ValueTask<long> GetChangeStampAsync(
        IDatabase database,
        ZLinkLocationChangeStampScope scope)
    {
        var value = await database.StringGetAsync(
            keys.StampKey(ZLinkRedisLocationKeys.TagOf(scope.Kind), scope.MeshName)).ConfigureAwait(false);
        return value.IsNull ? 0 : (long)value;
    }

    private static ZLinkLocationWriteResult ToWriteResult(RedisResult[] result)
    {
        var status = (string)result[0]!;
        return status switch
        {
            "stored" => ZLinkLocationWriteResult.Stored(
                (long)result[1],
                DateTimeOffset.FromUnixTimeMilliseconds((long)result[2])),
            "conflict" => ZLinkLocationWriteResult.RejectedConflict,
            _ => ZLinkLocationWriteResult.IgnoredStale
        };
    }
}
