using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Official Redis implementation of every location store contract. One
/// instance backs the peer, spot, actor, and route stores plus the owner
/// lease store and the change stamp store, which satisfies the contract
/// requirement that location rows and owner leases share one physical store
/// (draft 6.6): NewClaim judges "row owner's lease expired" atomically inside
/// a Lua script against the lease key's Redis TTL.
///
/// Behavior is contractually identical to the framework's in-memory store:
/// store-issued generations that survive row removal, owner-guarded removes,
/// store-clock UpdatedAt, and double change stamp bumps per write. Read APIs
/// surface store failures as exceptions; write APIs return
/// <see cref="ZLinkLocationWriteStatus.StoreUnavailable"/> instead.
/// </summary>
public sealed class ZLinkRedisLocationStore :
    IZLinkPeerLocationStore,
    IZLinkSpotLocationStore,
    IZLinkActorLocationStore,
    IZLinkRouteLocationStore,
    IZLinkOwnerLeaseStore,
    IZLinkLocationChangeStampStore,
    IAsyncDisposable
{
    private static readonly RedisValue[] RowFields = ["json", "gen", "updatedAtMs"];

    private static readonly Kind<ZLinkPeerLocation> PeerKind = new()
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

    private static readonly Kind<ZLinkSpotLocation> SpotKind = new()
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

    private static readonly Kind<ZLinkActorLocation> ActorKind = new()
    {
        Tag = "actor",
        EncodeKey = static row => ZLinkRedisLocationKeyCodec.EncodeActorKey(
            new ZLinkActorLocationKey(row.ActorType, row.ActorId)),
        MeshOf = static _ => null,
        OwnerOf = static row => row.OwnerId,
        GenerationOf = static row => row.Generation,
        Finalize = static (row, generation, updatedAt) =>
            row with { Generation = generation, UpdatedAt = updatedAt }
    };

    private static readonly Kind<ZLinkRouteLocation> RouteKind = new()
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

    private readonly ZLinkRedisLocationOptions _options;
    private readonly string _prefix;
    private readonly SemaphoreSlim _connectGate = new(1, 1);
    private ConnectionMultiplexer? _connection;

    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        options.Validate();
        _options = options;
        _prefix = options.KeyPrefix;
    }

    // ----- peer store ------------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(PeerKind, peer, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            PeerKind.Tag, ZLinkRedisLocationKeyCodec.EncodePeerKey(key), key.MeshName,
            owner, cancellationToken);

    ValueTask<long> IZLinkPeerLocationStore.RemoveByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken) =>
        RemoveByOwnerAsync(PeerKind.Tag, ownerId, cancellationToken);

    public async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        var members = await database.SetMembersAsync(KindIndexKey(PeerKind.Tag)).ConfigureAwait(false);
        var rows = await LoadRowsAsync(database, PeerKind, members).ConfigureAwait(false);
        return rows.Where(row => Matches(row, filter)).ToArray();
    }

    // ----- spot store ------------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(SpotKind, spot, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            SpotKind.Tag, ZLinkRedisLocationKeyCodec.EncodeSpotKey(key), key.MeshName,
            owner, cancellationToken);

    ValueTask<long> IZLinkSpotLocationStore.RemoveByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken) =>
        RemoveByOwnerAsync(SpotKind.Tag, ownerId, cancellationToken);

    public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default) =>
        ResolveAsync(SpotKind, ZLinkRedisLocationKeyCodec.EncodeSpotKey(key), cancellationToken);

    public ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default) =>
        ListPageAsync(SpotKind, row => Matches(row, filter), page, cancellationToken);

    // ----- actor store -----------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(ActorKind, actor, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            ActorKind.Tag, ZLinkRedisLocationKeyCodec.EncodeActorKey(key), meshName: null,
            owner, cancellationToken);

    ValueTask<long> IZLinkActorLocationStore.RemoveByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken) =>
        RemoveByOwnerAsync(ActorKind.Tag, ownerId, cancellationToken);

    public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default) =>
        ResolveAsync(ActorKind, ZLinkRedisLocationKeyCodec.EncodeActorKey(key), cancellationToken);

    public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default) =>
        ListPageAsync(ActorKind, row => Matches(row, filter), page, cancellationToken);

    // ----- route store -----------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(RouteKind, route, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            RouteKind.Tag, ZLinkRedisLocationKeyCodec.EncodeRouteKey(key), meshName: null,
            owner, cancellationToken);

    ValueTask<long> IZLinkRouteLocationStore.RemoveByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken) =>
        RemoveByOwnerAsync(RouteKind.Tag, ownerId, cancellationToken);

    public ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        CancellationToken cancellationToken = default) =>
        ResolveAsync(RouteKind, ZLinkRedisLocationKeyCodec.EncodeRouteKey(key), cancellationToken);

    public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default) =>
        ListPageAsync(RouteKind, row => Matches(row, filter), page, cancellationToken);

    // ----- owner lease store -----------------------------------------------

    public async ValueTask<ZLinkLocationWriteResult> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        // PX rejects non-positive values; a TTL that low means "already
        // expired", which one millisecond is close enough to.
        var ttlMs = Math.Max(1L, (long)leaseTtl.TotalMilliseconds);
        try
        {
            var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
            var nowMs = (long)await database.ScriptEvaluateAsync(
                ZLinkRedisLocationScripts.RenewLease,
                [LeaseKey(ownerId), LeaseIndexKey()],
                [ownerId, nodeRid.ToHex(), ttlMs]).ConfigureAwait(false);
            return ZLinkLocationWriteResult.Stored(0, FromUnixMs(nowMs));
        }
        catch (RedisException)
        {
            return ZLinkLocationWriteResult.StoreUnavailable;
        }
        catch (RedisTimeoutException)
        {
            return ZLinkLocationWriteResult.StoreUnavailable;
        }
    }

    public async ValueTask<ZLinkLocationWriteResult> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
            var nowMs = (long)await database.ScriptEvaluateAsync(
                ZLinkRedisLocationScripts.RemoveLease,
                [LeaseKey(ownerId), LeaseIndexKey()],
                [ownerId]).ConfigureAwait(false);
            return ZLinkLocationWriteResult.Stored(0, FromUnixMs(nowMs));
        }
        catch (RedisException)
        {
            return ZLinkLocationWriteResult.StoreUnavailable;
        }
        catch (RedisTimeoutException)
        {
            return ZLinkLocationWriteResult.StoreUnavailable;
        }
    }

    public async ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        var raw = (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.ListLeases,
            [LeaseIndexKey()],
            [LeaseKeyPrefix()]).ConfigureAwait(false))!;

        var storeNow = FromUnixMs((long)raw[0]);
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
            var renewedAt = FromUnixMs(long.Parse(value[(separator + 1)..]));
            leases.Add(new ZLinkOwnerLease(
                ownerId,
                nodeRid,
                storeNow + TimeSpan.FromMilliseconds(remainingMs),
                renewedAt));
        }

        return new ZLinkOwnerLeaseSnapshot(leases, storeNow);
    }

    // ----- change stamp store ----------------------------------------------

    public async ValueTask<long> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        var value = await database.StringGetAsync(
            StampKey(TagOf(scope.Kind), scope.MeshName)).ConfigureAwait(false);
        return value.IsNull ? 0 : (long)value;
    }

    public async ValueTask DisposeAsync()
    {
        var connection = Interlocked.Exchange(ref _connection, null);
        if (connection is not null)
        {
            await connection.DisposeAsync().ConfigureAwait(false);
        }

        _connectGate.Dispose();
    }

    // ----- shared write/read paths -----------------------------------------

    private async ValueTask<ZLinkLocationWriteResult> WriteAsync<TRow>(
        Kind<TRow> kind,
        TRow row,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken)
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
        try
        {
            var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
            var result = (RedisResult[])(await database.ScriptEvaluateAsync(
                ZLinkRedisLocationScripts.Write,
                [RowHashKey(kind.Tag, rowKey), GenerationKey(kind.Tag, rowKey), KindIndexKey(kind.Tag)],
                [
                    intentName,
                    kind.OwnerOf(row),
                    kind.GenerationOf(row),
                    json,
                    rowKey,
                    LeaseKeyPrefix(),
                    OwnerIndexKeyPrefix(kind.Tag),
                    StampKey(kind.Tag, meshName),
                    meshName is null ? string.Empty : StampKey(kind.Tag, meshName: null),
                    meshName is null ? "0" : "1",
                    meshName ?? string.Empty
                ]).ConfigureAwait(false))!;
            return ToWriteResult(result);
        }
        catch (RedisException)
        {
            return ZLinkLocationWriteResult.StoreUnavailable;
        }
        catch (RedisTimeoutException)
        {
            return ZLinkLocationWriteResult.StoreUnavailable;
        }
    }

    private async ValueTask<ZLinkLocationWriteResult> RemoveAsync(
        string tag,
        string rowKey,
        string? meshName,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken)
    {
        try
        {
            var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
            var result = (RedisResult[])(await database.ScriptEvaluateAsync(
                ZLinkRedisLocationScripts.Remove,
                [RowHashKey(tag, rowKey), KindIndexKey(tag)],
                [
                    owner.OwnerId,
                    owner.Generation,
                    rowKey,
                    OwnerIndexKeyPrefix(tag),
                    StampKey(tag, meshName),
                    meshName is null ? string.Empty : StampKey(tag, meshName: null)
                ]).ConfigureAwait(false))!;
            return ToWriteResult(result);
        }
        catch (RedisException)
        {
            return ZLinkLocationWriteResult.StoreUnavailable;
        }
        catch (RedisTimeoutException)
        {
            return ZLinkLocationWriteResult.StoreUnavailable;
        }
    }

    private async ValueTask<long> RemoveByOwnerAsync(
        string tag,
        string ownerId,
        CancellationToken cancellationToken)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        var removed = await database.ScriptEvaluateAsync(
            ZLinkRedisLocationScripts.RemoveByOwner,
            [(RedisKey)(OwnerIndexKeyPrefix(tag) + ownerId), KindIndexKey(tag)],
            [RowHashKeyPrefix(tag), StampKey(tag, meshName: null)]).ConfigureAwait(false);
        return (long)removed;
    }

    private async ValueTask<TRow?> ResolveAsync<TRow>(
        Kind<TRow> kind,
        string rowKey,
        CancellationToken cancellationToken)
        where TRow : class
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        var fields = await database.HashGetAsync(
            RowHashKey(kind.Tag, rowKey), RowFields).ConfigureAwait(false);
        return Materialize(kind, fields);
    }

    private async ValueTask<ZLinkLocationPage<TRow>> ListPageAsync<TRow>(
        Kind<TRow> kind,
        Func<TRow, bool> matches,
        ZLinkPageRequest page,
        CancellationToken cancellationToken)
        where TRow : class
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        RedisValue[] members;
        string? continuation = null;
        if (page.PageSize <= 0)
        {
            // No page size means the framework layer chose "unbounded", the
            // same contract the in-memory store applies.
            members = await database.SetMembersAsync(KindIndexKey(kind.Tag)).ConfigureAwait(false);
        }
        else
        {
            // The continuation token is the opaque SSCAN cursor; COUNT is a
            // hint, so pages are approximately PageSize rows before the
            // client-side field filter is applied.
            var cursor = page.ContinuationToken ?? "0";
            var scan = (RedisResult[])(await database.ExecuteAsync(
                "SSCAN", KindIndexKey(kind.Tag), cursor, "COUNT", page.PageSize)
                .ConfigureAwait(false))!;
            var nextCursor = (string)scan[0]!;
            members = (RedisValue[])scan[1]!;
            continuation = nextCursor == "0" ? null : nextCursor;
        }

        var rows = await LoadRowsAsync(database, kind, members).ConfigureAwait(false);
        return new ZLinkLocationPage<TRow>(rows.Where(matches).ToArray(), continuation);
    }

    private async Task<List<TRow>> LoadRowsAsync<TRow>(
        IDatabase database,
        Kind<TRow> kind,
        RedisValue[] rowKeys)
        where TRow : class
    {
        if (rowKeys.Length == 0)
        {
            return [];
        }

        var batch = database.CreateBatch();
        var reads = new Task<RedisValue[]>[rowKeys.Length];
        for (var i = 0; i < rowKeys.Length; i++)
        {
            reads[i] = batch.HashGetAsync(RowHashKey(kind.Tag, (string)rowKeys[i]!), RowFields);
        }

        batch.Execute();
        await Task.WhenAll(reads).ConfigureAwait(false);

        var rows = new List<TRow>(rowKeys.Length);
        foreach (var read in reads)
        {
            // A row listed by the index may have been removed concurrently;
            // skipping it matches a snapshot taken a moment later.
            if (Materialize(kind, read.Result) is { } row)
            {
                rows.Add(row);
            }
        }

        return rows;
    }

    private static TRow? Materialize<TRow>(Kind<TRow> kind, RedisValue[] fields)
        where TRow : class
    {
        if (fields[0].IsNull)
        {
            return null;
        }

        // The store-issued generation and store-clock update time always win
        // over whatever the writer serialized into the JSON payload.
        var row = ZLinkRedisLocationRowJson.Deserialize<TRow>((string)fields[0]!);
        return kind.Finalize(row, (long)fields[1], FromUnixMs((long)fields[2]));
    }

    private static ZLinkLocationWriteResult ToWriteResult(RedisResult[] result)
    {
        var status = (string)result[0]!;
        return status switch
        {
            "stored" => ZLinkLocationWriteResult.Stored(
                (long)result[1], FromUnixMs((long)result[2])),
            "conflict" => ZLinkLocationWriteResult.RejectedConflict,
            _ => ZLinkLocationWriteResult.IgnoredStale
        };
    }

    private async ValueTask<IDatabase> GetDatabaseAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (Volatile.Read(ref _connection) is { } connected)
        {
            return connected.GetDatabase();
        }

        await _connectGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var connection = _connection ??= await ConnectionMultiplexer
                .ConnectAsync(_options.BuildConfiguration())
                .ConfigureAwait(false);
            return connection.GetDatabase();
        }
        finally
        {
            _connectGate.Release();
        }
    }

    // ----- key schema -------------------------------------------------------

    private static string TagOf(ZLinkLocationKind kind) => kind switch
    {
        ZLinkLocationKind.Peer => PeerKind.Tag,
        ZLinkLocationKind.Spot => SpotKind.Tag,
        ZLinkLocationKind.Actor => ActorKind.Tag,
        ZLinkLocationKind.Route => RouteKind.Tag,
        _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown location kind.")
    };

    private RedisKey RowHashKey(string tag, string rowKey) => RowHashKeyPrefix(tag) + rowKey;

    private string RowHashKeyPrefix(string tag) => $"{_prefix}:row:{tag}:";

    private RedisKey GenerationKey(string tag, string rowKey) => $"{_prefix}:gen:{tag}:{rowKey}";

    private RedisKey KindIndexKey(string tag) => $"{_prefix}:keys:{tag}";

    private string OwnerIndexKeyPrefix(string tag) => $"{_prefix}:own:{tag}:";

    private RedisKey LeaseKey(string ownerId) => LeaseKeyPrefix() + ownerId;

    private string LeaseKeyPrefix() => $"{_prefix}:lease:";

    private RedisKey LeaseIndexKey() => $"{_prefix}:leases";

    private string StampKey(string tag, string? meshName) =>
        meshName is null ? $"{_prefix}:stamp:{tag}" : $"{_prefix}:stamp:{tag}:{meshName}";

    private static DateTimeOffset FromUnixMs(long unixMs) =>
        DateTimeOffset.FromUnixTimeMilliseconds(unixMs);

    // ----- filters (same semantics as the in-memory store) ------------------

    private static bool Matches(ZLinkPeerLocation row, ZLinkPeerLocationFilter filter) =>
        (filter.AutoConnectType is null || row.AutoConnectType == filter.AutoConnectType)
        && (filter.MeshName is null || row.MeshName == filter.MeshName)
        && (filter.Role is null || row.Role == filter.Role)
        && (filter.NodeRid is null || Equals(row.NodeRid, filter.NodeRid))
        && (filter.Endpoint is null || row.Endpoint == filter.Endpoint);

    private static bool Matches(ZLinkSpotLocation row, ZLinkSpotLocationFilter filter) =>
        (filter.MeshName is null || row.MeshName == filter.MeshName)
        && (filter.SpotType is null || row.SpotType == filter.SpotType)
        && (filter.NodeRid is null || row.NodeRid.Equals(filter.NodeRid.Value))
        && (filter.SpotKind is null || row.SpotKind == filter.SpotKind);

    private static bool Matches(ZLinkActorLocation row, ZLinkActorLocationFilter filter) =>
        (filter.ActorType is null || row.ActorType == filter.ActorType)
        && (filter.NodeRid is null || row.NodeRid.Equals(filter.NodeRid.Value))
        && (filter.SpotRid is null || Equals(row.SpotRid, filter.SpotRid))
        && (filter.LocationKind is null || row.LocationKind == filter.LocationKind);

    private static bool Matches(ZLinkRouteLocation row, ZLinkRouteLocationFilter filter) =>
        (filter.RouteKind is null || row.RouteKind == filter.RouteKind)
        && (filter.OwnerNodeRid is null || row.OwnerNodeRid.Equals(filter.OwnerNodeRid.Value))
        && (filter.OwnerId is null || row.OwnerId == filter.OwnerId);

    /// <summary>Per-kind row plumbing: canonical key encoding, mesh scope for
    /// change stamps, owner token access, and how store-issued generation and
    /// update time are applied to a loaded row.</summary>
    private sealed class Kind<TRow>
        where TRow : class
    {
        public required string Tag { get; init; }

        public required Func<TRow, string> EncodeKey { get; init; }

        public required Func<TRow, string?> MeshOf { get; init; }

        public required Func<TRow, string> OwnerOf { get; init; }

        public required Func<TRow, long> GenerationOf { get; init; }

        public required Func<TRow, long, DateTimeOffset, TRow> Finalize { get; init; }
    }
}
