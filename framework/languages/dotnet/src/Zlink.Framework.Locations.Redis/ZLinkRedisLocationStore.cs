using StackExchange.Redis;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Redis implementation of the framework location, owner lease, and change
/// stamp store contracts. It preserves store-issued generations, applies
/// owner-guarded mutations, uses store timestamps, and reports store failures
/// through the operation that encountered them.
/// </summary>
public sealed class ZLinkRedisLocationStore :
    IZLinkLocationStore,
    IZLinkRoutingIdSlotAllocationStore,
    IZLinkLocationChangeStampStore,
    IAsyncDisposable
{
    private readonly ZLinkRedisLocationOptions _options;
    private readonly ZLinkRedisLocationKeys _keys;
    private readonly ZLinkRedisLocationCommands _commands;
    private readonly Func<ConfigurationOptions, ValueTask<IZLinkRedisConnection>> _connect;
    private readonly SemaphoreSlim _connectGate = new(1, 1);
    private readonly object _disposeGate = new();
    private IZLinkRedisConnection? _connection;
    private Task? _disposeTask;
    private TaskCompletionSource? _operationsDrained;
    private int _activeOperations;
    private int _disposed;

    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options)
        : this(options, ConnectAsync)
    {
    }

    internal ZLinkRedisLocationStore(
        ZLinkRedisLocationOptions options,
        Func<ConfigurationOptions, ValueTask<IZLinkRedisConnection>> connect)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(connect);
        options.Validate();
        _options = options;
        _keys = new ZLinkRedisLocationKeys(options.KeyPrefix);
        _commands = new ZLinkRedisLocationCommands(_keys);
        _connect = connect;
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
        return await ExecuteAsync(
                async database =>
                {
                    var members = await database.SetMembersAsync(
                            _keys.KindIndexKey(ZLinkRedisLocationKinds.Peer.Tag))
                        .ConfigureAwait(false);
                    var rows = await ZLinkRedisLocationRows.LoadAsync(
                            database,
                            _keys,
                            ZLinkRedisLocationKinds.Peer,
                            members)
                        .ConfigureAwait(false);
                    return rows.Where(row => ZLinkLocationFilterMatcher.Matches(row, filter)).ToArray();
                },
                cancellationToken)
            .ConfigureAwait(false);
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
        ListPageAsync(
            ZLinkRedisLocationKinds.Spot,
            row => ZLinkLocationFilterMatcher.Matches(row, filter),
            page,
            cancellationToken);

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
        ListPageAsync(
            ZLinkRedisLocationKinds.Actor,
            row => ZLinkLocationFilterMatcher.Matches(row, filter),
            page,
            cancellationToken);

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
        ListPageAsync(
            ZLinkRedisLocationKinds.Route,
            row => ZLinkLocationFilterMatcher.Matches(row, filter),
            page,
            cancellationToken);

    // ----- owner lease store -----------------------------------------------

    public async ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteAsync(
                database => _commands.RenewOwnerLeaseAsync(database, ownerId, nodeRid, leaseTtl),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteAsync(
                database => _commands.RemoveOwnerLeaseAsync(database, ownerId),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteAsync(
                database => _commands.RemoveAllByOwnerAsync(database, ownerId),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default)
    {
        return await ExecuteAsync(
                _commands.ListOwnerLeasesAsync,
                cancellationToken)
            .ConfigureAwait(false);
    }

    // ----- change stamp store ----------------------------------------------

    public async ValueTask<long> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteAsync(
                database => _commands.GetChangeStampAsync(database, scope),
                cancellationToken)
            .ConfigureAwait(false);
    }

    // ----- routing-id slot allocation ------------------------------------

    public async ValueTask<ZLinkRoutingIdSlotAcquireResult> AcquireRoutingIdSlotAsync(
        ZLinkRoutingIdSlotAcquireRequest request,
        CancellationToken cancellationToken = default)
    {
        ZLinkRoutingIdSlotAllocationValidator.ValidateAcquire(request);
        return await ExecuteAsync(
                database => _commands.AcquireRoutingIdSlotAsync(database, request),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkRoutingIdSlotReleaseResult> ReleaseRoutingIdSlotAsync(
        string groupName,
        int slot,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        ZLinkRoutingIdSlotAllocationValidator.ValidateRelease(groupName, slot, owner);
        return await ExecuteAsync(
                database => _commands.ReleaseRoutingIdSlotAsync(database, groupName, slot, owner),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkRoutingIdSlotAllocationSnapshot> ListRoutingIdSlotsAsync(
        string groupName,
        CancellationToken cancellationToken = default)
    {
        ZLinkRoutingIdSlotAllocationValidator.ValidateGroupName(groupName);
        return await ExecuteAsync(
                database => _commands.ListRoutingIdSlotsAsync(database, groupName),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask DisposeAsync()
    {
        Task disposeTask;
        TaskCompletionSource? startDispose = null;
        lock (_disposeGate)
        {
            if (_disposeTask is null)
            {
                Volatile.Write(ref _disposed, 1);
                startDispose = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _disposeTask = DisposeCoreAsync(startDispose.Task);
            }

            disposeTask = _disposeTask;
        }

        startDispose?.TrySetResult();
        return new ValueTask(disposeTask);
    }

    private async Task DisposeCoreAsync(Task started)
    {
        await started.ConfigureAwait(false);
        Task? operationsDrained;
        lock (_disposeGate)
        {
            operationsDrained = _activeOperations == 0
                ? null
                : (_operationsDrained ??= new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously)).Task;
        }

        if (operationsDrained is not null) await operationsDrained.ConfigureAwait(false);

        var connection = _connection;
        _connection = null;
        try
        {
            if (connection is not null) await connection.DisposeAsync().ConfigureAwait(false);
        }
        finally
        {
            _connectGate.Dispose();
        }
    }

    // ----- shared write/read paths -----------------------------------------

    private async ValueTask<ZLinkLocationWriteResult> WriteAsync<TRow>(
        ZLinkRedisLocationKind<TRow> kind,
        TRow row,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken)
        where TRow : class
    {
        return await ExecuteAsync(
                database => _commands.WriteAsync(database, kind, row, intent),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkLocationWriteResult> RemoveAsync(
        string tag,
        string rowKey,
        string? meshName,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken)
    {
        return await ExecuteAsync(
                database => _commands.RemoveAsync(database, tag, rowKey, meshName, owner),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<TRow?> ResolveAsync<TRow>(
        ZLinkRedisLocationKind<TRow> kind,
        string rowKey,
        CancellationToken cancellationToken)
        where TRow : class
    {
        return await ExecuteAsync(
                async database =>
                {
                    var fields = await database.HashGetAsync(
                            _keys.RowHashKey(kind.Tag, rowKey),
                            ZLinkRedisLocationRows.Fields)
                        .ConfigureAwait(false);
                    return ZLinkRedisLocationRows.Materialize(kind, fields);
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkLocationPage<TRow>> ListPageAsync<TRow>(
        ZLinkRedisLocationKind<TRow> kind,
        Func<TRow, bool> matches,
        ZLinkPageRequest page,
        CancellationToken cancellationToken)
        where TRow : class
    {
        return await ExecuteAsync(
                async database =>
                {
                    RedisValue[] members;
                    string? continuation = null;
                    if (page.PageSize <= 0)
                    {
                        // No page size means the framework layer chose "unbounded", the
                        // same contract the in-memory store applies.
                        members = await database.SetMembersAsync(_keys.KindIndexKey(kind.Tag))
                            .ConfigureAwait(false);
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

                    var rows = await ZLinkRedisLocationRows.LoadAsync(database, _keys, kind, members)
                        .ConfigureAwait(false);
                    return new ZLinkLocationPage<TRow>(rows.Where(matches).ToArray(), continuation);
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<TResult> ExecuteAsync<TResult>(
        Func<IDatabase, ValueTask<TResult>> operation,
        CancellationToken cancellationToken)
    {
        using var lease = EnterOperation();
        cancellationToken.ThrowIfCancellationRequested();
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        return await operation(database).ConfigureAwait(false);
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
            var connection = _connection ??= await _connect(_options.BuildConfiguration())
                .ConfigureAwait(false);
            return connection.GetDatabase();
        }
        finally
        {
            _connectGate.Release();
        }
    }

    private OperationLease EnterOperation()
    {
        lock (_disposeGate)
        {
            ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
            _activeOperations++;
            return new OperationLease(this);
        }
    }

    private void ExitOperation()
    {
        TaskCompletionSource? drained = null;
        lock (_disposeGate)
        {
            _activeOperations--;
            if (_activeOperations == 0 && Volatile.Read(ref _disposed) != 0)
            {
                drained = _operationsDrained;
                _operationsDrained = null;
            }
        }

        drained?.TrySetResult();
    }

    private sealed class OperationLease(ZLinkRedisLocationStore owner) : IDisposable
    {
        private ZLinkRedisLocationStore? _owner = owner;

        public void Dispose() => Interlocked.Exchange(ref _owner, null)?.ExitOperation();
    }

    private static async ValueTask<IZLinkRedisConnection> ConnectAsync(ConfigurationOptions options) =>
        new ZLinkStackExchangeRedisConnection(
            await ConnectionMultiplexer.ConnectAsync(options).ConfigureAwait(false));

}

internal interface IZLinkRedisConnection : IAsyncDisposable
{
    IDatabase GetDatabase();
}

internal sealed class ZLinkStackExchangeRedisConnection(ConnectionMultiplexer connection)
    : IZLinkRedisConnection
{
    public IDatabase GetDatabase() => connection.GetDatabase();

    public ValueTask DisposeAsync() => connection.DisposeAsync();
}
