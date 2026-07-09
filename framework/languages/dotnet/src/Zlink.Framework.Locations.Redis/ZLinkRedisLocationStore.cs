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
/// surface store failures as exceptions for both reads and writes.
/// </summary>
public sealed class ZLinkRedisLocationStore :
    IZLinkLocationStore,
    IZLinkLocationChangeStampStore,
    IAsyncDisposable
{
    private readonly ZLinkRedisLocationOptions _options;
    private readonly ZLinkRedisLocationKeys _keys;
    private readonly ZLinkRedisLocationCommands _commands;
    private readonly SemaphoreSlim _connectGate = new(1, 1);
    private ConnectionMultiplexer? _connection;

    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        options.Validate();
        _options = options;
        _keys = new ZLinkRedisLocationKeys(options.KeyPrefix);
        _commands = new ZLinkRedisLocationCommands(_keys);
    }

    /// <summary>
    /// Builder-style configuration, matching the framework's channel and
    /// mesh builders: <c>new ZLinkRedisLocationStore(redis => redis
    /// .SetConnectionString(...).SetKeyPrefix(...))</c>.
    /// </summary>
    public ZLinkRedisLocationStore(Action<ZLinkRedisLocationOptions> configure)
        : this(Configure(configure))
    {
    }

    private static ZLinkRedisLocationOptions Configure(Action<ZLinkRedisLocationOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(configure);
        var options = new ZLinkRedisLocationOptions();
        configure(options);
        return options;
    }

    // ----- peer store ------------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(ZLinkRedisLocationKinds.Peer, peer, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            ZLinkRedisLocationKinds.Peer.Tag,
            ZLinkRedisLocationKeyCodec.EncodePeerKey(key),
            key.MeshName,
            owner, cancellationToken);

    public async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        var members = await database.SetMembersAsync(_keys.KindIndexKey(ZLinkRedisLocationKinds.Peer.Tag)).ConfigureAwait(false);
        var rows = await ZLinkRedisLocationRows.LoadAsync(database, _keys, ZLinkRedisLocationKinds.Peer, members).ConfigureAwait(false);
        return rows.Where(row => Matches(row, filter)).ToArray();
    }

    // ----- spot store ------------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(ZLinkRedisLocationKinds.Spot, spot, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            ZLinkRedisLocationKinds.Spot.Tag,
            ZLinkRedisLocationKeyCodec.EncodeSpotKey(key),
            key.MeshName,
            owner, cancellationToken);

    public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default) =>
        ResolveAsync(ZLinkRedisLocationKinds.Spot, ZLinkRedisLocationKeyCodec.EncodeSpotKey(key), cancellationToken);

    public ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default) =>
        ListPageAsync(ZLinkRedisLocationKinds.Spot, row => Matches(row, filter), page, cancellationToken);

    // ----- actor store -----------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(ZLinkRedisLocationKinds.Actor, actor, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            ZLinkRedisLocationKinds.Actor.Tag,
            ZLinkRedisLocationKeyCodec.EncodeActorKey(key),
            meshName: null,
            owner, cancellationToken);

    public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default) =>
        ResolveAsync(ZLinkRedisLocationKinds.Actor, ZLinkRedisLocationKeyCodec.EncodeActorKey(key), cancellationToken);

    public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default) =>
        ListPageAsync(ZLinkRedisLocationKinds.Actor, row => Matches(row, filter), page, cancellationToken);

    // ----- route store -----------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(ZLinkRedisLocationKinds.Route, route, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            ZLinkRedisLocationKinds.Route.Tag,
            ZLinkRedisLocationKeyCodec.EncodeRouteKey(key),
            meshName: null,
            owner, cancellationToken);

    public ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        CancellationToken cancellationToken = default) =>
        ResolveAsync(ZLinkRedisLocationKinds.Route, ZLinkRedisLocationKeyCodec.EncodeRouteKey(key), cancellationToken);

    public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default) =>
        ListPageAsync(ZLinkRedisLocationKinds.Route, row => Matches(row, filter), page, cancellationToken);

    // ----- owner lease store -----------------------------------------------

    public async ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        return await _commands.RenewOwnerLeaseAsync(database, ownerId, nodeRid, leaseTtl)
            .ConfigureAwait(false);
    }

    public async ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        return await _commands.RemoveOwnerLeaseAsync(database, ownerId).ConfigureAwait(false);
    }

    public async ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        return await _commands.RemoveAllByOwnerAsync(database, ownerId).ConfigureAwait(false);
    }

    public async ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        return await _commands.ListOwnerLeasesAsync(database).ConfigureAwait(false);
    }

    // ----- change stamp store ----------------------------------------------

    public async ValueTask<long> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        return await _commands.GetChangeStampAsync(database, scope).ConfigureAwait(false);
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
        ZLinkRedisLocationKind<TRow> kind,
        TRow row,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken)
        where TRow : class
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        return await _commands.WriteAsync(database, kind, row, intent).ConfigureAwait(false);
    }

    private async ValueTask<ZLinkLocationWriteResult> RemoveAsync(
        string tag,
        string rowKey,
        string? meshName,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken)
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        return await _commands.RemoveAsync(database, tag, rowKey, meshName, owner).ConfigureAwait(false);
    }

    private async ValueTask<TRow?> ResolveAsync<TRow>(
        ZLinkRedisLocationKind<TRow> kind,
        string rowKey,
        CancellationToken cancellationToken)
        where TRow : class
    {
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        var fields = await database.HashGetAsync(
            _keys.RowHashKey(kind.Tag, rowKey),
            ZLinkRedisLocationRows.Fields).ConfigureAwait(false);
        return ZLinkRedisLocationRows.Materialize(kind, fields);
    }

    private async ValueTask<ZLinkLocationPage<TRow>> ListPageAsync<TRow>(
        ZLinkRedisLocationKind<TRow> kind,
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
            members = await database.SetMembersAsync(_keys.KindIndexKey(kind.Tag)).ConfigureAwait(false);
        }
        else
        {
            // The continuation token is the opaque SSCAN cursor; COUNT is a
            // hint, so pages are approximately PageSize rows before the
            // client-side field filter is applied.
            var cursor = page.ContinuationToken ?? "0";
            var scan = (RedisResult[])(await database.ExecuteAsync(
                "SSCAN", _keys.KindIndexKey(kind.Tag), cursor, "COUNT", page.PageSize)
                .ConfigureAwait(false))!;
            var nextCursor = (string)scan[0]!;
            members = (RedisValue[])scan[1]!;
            continuation = nextCursor == "0" ? null : nextCursor;
        }

        var rows = await ZLinkRedisLocationRows.LoadAsync(database, _keys, kind, members).ConfigureAwait(false);
        return new ZLinkLocationPage<TRow>(rows.Where(matches).ToArray(), continuation);
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

}
