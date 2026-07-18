using StackExchange.Redis;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Redis implementation of the framework location, owner lease, and change
/// stamp store contracts. It preserves store-issued generations, applies
/// owner-guarded mutations, uses store timestamps, and reports store failures
/// through the operation that encountered them.
/// </summary>
public sealed partial class ZLinkRedisLocationStore :
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

    // ----- mesh node store -------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(ZLinkRedisLocationKinds.MeshNode, descriptor, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            ZLinkRedisLocationKinds.MeshNode.Tag,
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(key),
            key.MeshName,
            owner, cancellationToken);

    public async ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteAsync(
                async database =>
                {
                    var members = await database.SetMembersAsync(
                            _keys.KindIndexKey(ZLinkRedisLocationKinds.MeshNode.Tag))
                        .ConfigureAwait(false);
                    var rows = await ZLinkRedisLocationRows.LoadAsync(
                            database,
                            _keys,
                            ZLinkRedisLocationKinds.MeshNode,
                            members)
                        .ConfigureAwait(false);
                    return (IReadOnlyList<ZLinkMeshNodeDescriptor>)rows
                        .Where(row => string.Equals(row.MeshName, meshName, StringComparison.Ordinal))
                        .ToArray();
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

    public ValueTask<ZLinkLocationWriteStatus> RemoveSpotAsync(
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

    // ----- actor store -----------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(ZLinkRedisLocationKinds.Actor, actor, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            ZLinkRedisLocationKinds.Actor.Tag,
            ZLinkRedisLocationKeyCodec.EncodeActorKey(key),
            key.MeshName,
            owner, cancellationToken);

    public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default) =>
        ResolveAsync(ZLinkRedisLocationKinds.Actor, ZLinkRedisLocationKeyCodec.EncodeActorKey(key), cancellationToken);

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

    public async ValueTask<ulong> GetChangeStampAsync(
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

    private async ValueTask<ZLinkLocationWriteStatus> RemoveAsync(
        string tag,
        string rowKey,
        string? meshName,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken)
    {
        var result = await ExecuteAsync(
                database => _commands.RemoveAsync(database, tag, rowKey, meshName, owner),
                cancellationToken)
            .ConfigureAwait(false);
        return result.Status;
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
