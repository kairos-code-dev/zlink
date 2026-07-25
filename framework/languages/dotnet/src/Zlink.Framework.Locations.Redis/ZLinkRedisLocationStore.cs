using StackExchange.Redis;
using Zlink.Framework.Runtime.Configuration;
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
    IZLinkClientServerLocationStore,
    IZLinkFanoutLocationStore,
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

    public async ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteAsync(
                database => _commands.WriteMeshNodeAsync(
                    database,
                    descriptor,
                    intent),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        ExecuteAsync(
            async database =>
            {
                var result = await _commands.RemoveMeshNodeAsync(
                        database,
                        ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(key),
                        key.MeshName,
                        owner)
                    .ConfigureAwait(false);
                return result.Status;
            },
            cancellationToken);

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
                    var selected = rows
                        .Where(row => string.Equals(row.MeshName, meshName, StringComparison.Ordinal))
                        .ToArray();
                    for (var index = 0; index < selected.Length; index++)
                    {
                        var row = selected[index];
                        var actorCapacity = await ReadCapacityProjectionAsync(
                                database,
                                CapacityPopulationBucket(
                                    row,
                                    ZLinkPlacementObjectKind.Actor),
                                string.Empty)
                            .ConfigureAwait(false);
                        var spotBucket = CapacityPopulationBucket(
                            row,
                            ZLinkPlacementObjectKind.UserSpot);
                        RedisResult[]? spotCapacity = null;
                        var spotTypes = new List<ZLinkSpotTypeCapacity>();
                        foreach (var capability in row.ObjectCapabilities)
                        {
                            if (capability.ObjectKind
                                is not (ZLinkPlacementObjectKind.UserSpot
                                    or ZLinkPlacementObjectKind.InstanceSpot))
                                continue;
                            var capacity = await ReadCapacityProjectionAsync(
                                    database,
                                    spotBucket,
                                    CapacityTypeBucket(row, capability))
                                .ConfigureAwait(false);
                            spotCapacity ??= capacity;
                            spotTypes.Add(new ZLinkSpotTypeCapacity(
                                capability.ObjectKind,
                                capability.StableType,
                                ParseCapacity(capacity[2]),
                                ParseCapacity(capacity[3]),
                                capability.Limit));
                        }
                        spotCapacity ??= await ReadCapacityProjectionAsync(
                                database,
                                spotBucket,
                                string.Empty)
                            .ConfigureAwait(false);
                        selected[index] = row with
                        {
                            Capacity = new ZLinkPlacementCapacity(
                                row.Capacity.Actors with
                                {
                                    Active = ParseCapacity(actorCapacity[0]),
                                    Reserved = ParseCapacity(actorCapacity[1])
                                },
                                row.Capacity.Spots with
                                {
                                    Active = ParseCapacity(spotCapacity[0]),
                                    Reserved = ParseCapacity(spotCapacity[1])
                                },
                                spotTypes)
                        };
                    }
                    return (IReadOnlyList<ZLinkMeshNodeDescriptor>)selected;
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<RedisResult[]> ReadCapacityProjectionAsync(
        IDatabase database,
        string populationBucket,
        string typeBucket) =>
        (RedisResult[])(await database.ScriptEvaluateAsync(
            ZLinkRedisAuthorityScripts.ReadCapacityProjection,
            [
                _keys.HybridCapacityKey(type: false, pending: false),
                _keys.HybridCapacityKey(type: false, pending: true),
                _keys.HybridCapacityKey(type: true, pending: false),
                _keys.HybridCapacityKey(type: true, pending: true)
            ],
            [populationBucket, typeBucket]).ConfigureAwait(false))!;

    private static string CapacityPopulationBucket(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkPlacementObjectKind objectKind) =>
        ZLinkRedisLocationKeys.HybridCapacityPopulationBucket(
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
                new ZLinkMeshNodeDescriptorKey(
                    descriptor.MeshName,
                    descriptor.Rid)),
            descriptor.LifecycleGeneration,
            objectKind);

    private static string CapacityTypeBucket(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkObjectCapability capability) =>
        ZLinkRedisLocationKeys.HybridCapacityTypeBucket(
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
                new ZLinkMeshNodeDescriptorKey(
                    descriptor.MeshName,
                    descriptor.Rid)),
            descriptor.LifecycleGeneration,
            capability.ObjectKind,
            capability.StableType);

    private static int ParseCapacity(RedisResult value) =>
        int.Parse(
            (string)value!,
            System.Globalization.CultureInfo.InvariantCulture);

    // ----- ClientServer server descriptor store ---------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateClientServerAsync(
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        ValidateClientServerDescriptor(descriptor);
        return ExecuteAsync(
            database => _commands.WriteClientServerAsync(
                database,
                descriptor,
                intent),
            cancellationToken);
    }

    public ValueTask<ZLinkLocationWriteStatus> RemoveClientServerAsync(
        ZLinkClientServerServerDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key.ChannelName);
        if (key.ServerRid.IsEmpty)
            throw new ArgumentOutOfRangeException(nameof(key));
        return ExecuteAsync(
            async database =>
            {
                var result = await _commands.RemoveClientServerAsync(
                        database,
                        ZLinkRedisLocationKeyCodec.EncodeClientServerKey(key),
                        key.ChannelName,
                        owner)
                    .ConfigureAwait(false);
                return result.Status;
            },
            cancellationToken);
    }

    public ValueTask<ZLinkLocationPage<ZLinkClientServerServerDescriptor>>
        ListClientServersAsync(
            string channelName,
            ZLinkPageRequest page,
            CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        var pageSize = page.PageSize <= 0 ? 100 : page.PageSize;
        if (pageSize > 1000)
            throw new ArgumentOutOfRangeException(nameof(page));
        var offset = 0L;
        if (page.ContinuationToken is { } token
            && (!long.TryParse(
                    token,
                    System.Globalization.NumberStyles.None,
                    System.Globalization.CultureInfo.InvariantCulture,
                    out offset)
                || offset < 0))
            throw new ArgumentException(
                "The ClientServer continuation token is invalid.",
                nameof(page));

        return ExecuteAsync(
            async database =>
            {
                var members = await database.SortedSetRangeByRankAsync(
                        _keys.ClientServerChannelIndexKey(channelName),
                        offset,
                        checked(offset + pageSize))
                    .ConfigureAwait(false);
                var rows = new List<ZLinkClientServerServerDescriptor>(
                    Math.Min(pageSize, members.Length));
                var encodedBytes = 0;
                var consumedMembers = 0;
                foreach (var member in members)
                {
                    if (rows.Count == pageSize)
                        break;

                    var fields = await database.HashGetAsync(
                            _keys.RowHashKey(
                                ZLinkRedisLocationKinds.ClientServer.Tag,
                                (string)member!),
                            ZLinkRedisLocationRows.Fields)
                        .ConfigureAwait(false);
                    if (fields[0].IsNull)
                    {
                        consumedMembers++;
                        continue;
                    }

                    var rowBytes = System.Text.Encoding.UTF8.GetByteCount(
                        (string)fields[0]!);
                    if (rows.Count != 0
                        && encodedBytes + rowBytes > 4 * 1024 * 1024)
                    {
                        break;
                    }

                    if (ZLinkRedisLocationRows.Materialize(
                            ZLinkRedisLocationKinds.ClientServer,
                            fields) is { } row)
                    {
                        rows.Add(row);
                        encodedBytes += rowBytes;
                    }
                    consumedMembers++;
                }

                var next = consumedMembers < members.Length
                           || members.Length > pageSize
                    ? checked(offset + consumedMembers).ToString(
                        System.Globalization.CultureInfo.InvariantCulture)
                    : null;
                return new ZLinkLocationPage<ZLinkClientServerServerDescriptor>(
                    rows,
                    next);
            },
            cancellationToken);
    }

    // ----- spot store ------------------------------------------------------

    public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        WriteAsync(
            ZLinkRedisLocationKinds.Spot,
            spot,
            intent,
            cancellationToken,
            spot.SpotId);

    public ValueTask<ZLinkLocationWriteStatus> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        RemoveAsync(
            ZLinkRedisLocationKinds.Spot.Tag,
            ZLinkRedisLocationKeyCodec.EncodeSpotKey(key),
            meshName: null,
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
            null,
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

    public async ValueTask<ZLinkOwnerLeaseClaimResult> ClaimOwnerLeaseAsync(
        string ownerId,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(ownerId);
        if (leaseTtl <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(leaseTtl));
        return await ExecuteAsync(
                database => _commands.ClaimOwnerLeaseAsync(
                    database,
                    ownerId,
                    leaseTtl),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(ownerId);
        return await ExecuteAsync(
                database => _commands.ReadOwnerLeaseAsync(database, ownerId),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkOwnerLeaseRenewResult> RenewOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(token.OwnerId);
        if (token.LeaseGeneration <= 0 || leaseTtl <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(token));
        return await ExecuteAsync(
                database => _commands.RenewOwnerLeaseAsync(
                    database,
                    token,
                    leaseTtl),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkOwnerLeaseReleaseResult> ReleaseOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(token.OwnerId);
        if (token.LeaseGeneration <= 0)
            throw new ArgumentOutOfRangeException(nameof(token));
        return await ExecuteAsync(
                database => _commands.ReleaseOwnerLeaseAsync(database, token),
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
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(owner.OwnerId);
        if (owner.LeaseGeneration <= 0)
            throw new ArgumentOutOfRangeException(nameof(owner));
        return await ExecuteAsync(
                database => _commands.RemoveAllByOwnerAsync(database, owner),
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
        CancellationToken cancellationToken,
        string? spotId = null)
        where TRow : class
    {
        return await ExecuteAsync(
                database => _commands.WriteAsync(
                    database,
                    kind,
                    row,
                    intent,
                    spotId),
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

    private static void ValidateClientServerDescriptor(
        ZLinkClientServerServerDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.ChannelName);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.Endpoint);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.SecurityIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.OwnerId);
        if (descriptor.ServerRid.IsEmpty
            || descriptor.LifecycleGeneration == 0
            || descriptor.DescriptorRevision == 0
            || descriptor.LeaseGeneration <= 0
            || descriptor.Weight is < 0 or > ZLinkSocketConfig.MaximumPeerWeight
            || !Enum.IsDefined(descriptor.State))
            throw new ArgumentOutOfRangeException(nameof(descriptor));
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
