namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Executes the connect/disconnect decisions of the reconciler. The channel
/// runtime implements this over core socket calls; stores never touch
/// sockets. Connect failures are the executor's concern (retry backoff) and
/// are never a reason to remove a location row.
/// </summary>
internal interface IZLinkAutoConnectExecutor
{
    bool Connect(ZLinkAutoConnectTarget target);

    bool Disconnect(ZLinkAutoConnectTarget target);
}

/// <summary>
/// Per-mesh reconcile loop state machine. Reads the peer list through the
/// resolver, computes the desired target set, and diffs it against the
/// active set. Store failures are fail-static: the last desired set stays,
/// no diff runs, and after recovery the local row is re-published first and
/// disconnects wait one heartbeat interval so peers that have not
/// re-registered yet are not cut off in one sweep.
/// </summary>
internal sealed class ZLinkAutoConnectReconciler
{
    private readonly ZLinkAutoConnectLocal _local;
    private ZLinkPeerLocation? _localRow;
    private readonly ZLinkLocationRuntime _runtime;
    private readonly IZLinkPeerLocationResolver _peers;
    private readonly IZLinkAutoConnectExecutor _executor;
    private readonly ZLinkLocationOptions _options;
    private readonly ZLinkLocationEventEmitter _events;
    private readonly TimeProvider _time;
    private readonly SemaphoreSlim _reconcileGate = new(1, 1);
    private IDisposable? _peerMetricRegistration;
    private readonly Dictionary<string, ZLinkAutoConnectTarget> _active = new(StringComparer.Ordinal);
    private Dictionary<string, ZLinkAutoConnectTarget> _lastDesired = new(StringComparer.Ordinal);
    private volatile HashSet<string>? _meshMemberRids;
    private long _localGeneration;
    private bool _localPublished;
    private bool _storeFailed;
    private long? _storeFailureStartedAt;
    private long _recoveryDeferUntil;
    private bool _ownerCleanupStarted;
    private long _discoveredPeerCount;
    private long _pendingLocalWeight = -1;

    /// <summary>
    /// <paramref name="localRow"/> is null for a dial-only capability that
    /// has neither a routing id nor an endpoint (a client dealer or a
    /// subscriber without a configured identity): it cannot be keyed or
    /// advertised, but its reconcile loop still dials remote rows.
    /// </summary>
    internal ZLinkAutoConnectReconciler(
        ZLinkAutoConnectLocal local,
        ZLinkPeerLocation? localRow,
        ZLinkLocationRuntime runtime,
        IZLinkPeerLocationResolver peers,
        IZLinkAutoConnectExecutor executor,
        ZLinkLocationOptions options,
        TimeProvider? timeProvider = null,
        ZLinkLocationEventEmitter? events = null)
    {
        _local = local;
        _localRow = localRow;
        _runtime = runtime;
        _peers = peers;
        _executor = executor;
        _options = options;
        _events = events ?? ZLinkLocationEventEmitter.Disabled;
        _time = timeProvider ?? TimeProvider.System;
    }

    internal IReadOnlyCollection<ZLinkAutoConnectTarget> ActiveTargets => _active.Values;

    /// <summary>
    /// True when the last reconcile saw this node rid as a mesh member
    /// (any live row of the mesh, whichever side dials). Null before the
    /// first successful tick — no judgment possible yet.
    /// </summary>
    internal bool? KnowsPeer(RoutingId nodeRid)
    {
        if (_meshMemberRids is not { } members) return null;

        return members.Contains(nodeRid.ToHex());
    }

    /// <summary>True while the last tick could not read the store. The loop
    /// must not let a change stamp skip ticks in this state.</summary>
    internal bool StoreFailed => _storeFailed;

    internal void RegisterPeerMetric()
    {
        _peerMetricRegistration ??= ZLinkRuntimeMetrics.RegisterLocationPeers(
            () => Volatile.Read(ref _discoveredPeerCount));
    }

    internal void RemovePeerMetric()
    {
        Interlocked.Exchange(ref _peerMetricRegistration, null)?.Dispose();
    }

    internal void SetLocalWeight(uint weight) => Volatile.Write(ref _pendingLocalWeight, weight);

    internal ValueTask<bool> SetLocalWeightAsync(
        uint weight,
        CancellationToken cancellationToken = default) =>
        PublishLocalMutationAsync(
            row => row with { Weight = weight },
            cancellationToken);

    internal bool HasPendingTargets
    {
        get
        {
            var pendingWeight = Volatile.Read(ref _pendingLocalWeight);
            if (_localRow is { } localRow
                && pendingWeight >= 0
                && localRow.Weight != (uint)pendingWeight)
                return true;

            foreach (var (key, desired) in _lastDesired)
                if (!_active.TryGetValue(key, out var active)
                    || RequiresTargetRefresh(active, desired)) return true;
            return false;
        }
    }

    internal async ValueTask<bool> MarkDrainingAsync(
        CancellationToken cancellationToken = default)
        => await PublishLocalMutationAsync(
            row => row with { Draining = true },
            cancellationToken).ConfigureAwait(false);

    private async ValueTask<bool> PublishLocalMutationAsync(
        Func<ZLinkPeerLocation, ZLinkPeerLocation> mutation,
        CancellationToken cancellationToken)
    {
        await _reconcileGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_localRow is null) return true;
            _localRow = mutation(_localRow);
            if (!_localPublished)
                await PublishLocalAsync(cancellationToken).ConfigureAwait(false);
            if (!_localPublished || _localGeneration <= 0) return false;

            var result = await _runtime.WritePeerAsync(
                    _localRow with { Generation = _localGeneration },
                    ZLinkLocationWriteIntent.Renew,
                    cancellationToken)
                .ConfigureAwait(false);
            _localPublished = result.Status == ZLinkLocationWriteStatus.Stored;
            if (_localPublished) _localGeneration = result.Generation;
            return _localPublished;
        }
        finally
        {
            _reconcileGate.Release();
        }
    }

    internal async ValueTask FreezeOwnerWritesAsync(CancellationToken cancellationToken)
    {
        await _reconcileGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            _ownerCleanupStarted = true;
        }
        finally
        {
            _reconcileGate.Release();
        }
    }

    internal async ValueTask TickAsync(CancellationToken cancellationToken = default)
    {
        await _reconcileGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await TickCoreAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _reconcileGate.Release();
        }
    }

    private async ValueTask TickCoreAsync(CancellationToken cancellationToken)
    {
        if (_ownerCleanupStarted) return;
        var pendingWeight = Volatile.Read(ref _pendingLocalWeight);
        if (_localRow is { } localRow
            && pendingWeight >= 0
            && localRow.Weight != (uint)pendingWeight)
        {
            _localRow = localRow with { Weight = (uint)pendingWeight };
            _localPublished = false;
        }
        // Publish (or re-publish after recovery) the local row before
        // reading the list, so peers observing the store during our
        // recovery window can already see us.
        IReadOnlyList<ZLinkPeerLocation> rows;
        try
        {
            await PublishLocalAsync(cancellationToken).ConfigureAwait(false);
            rows = await _peers.ListLivePeersAsync(
                new ZLinkPeerLocationFilter(
                    AutoConnectType: _local.AutoConnectType,
                    MeshName: _local.MeshName),
                cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception)
        {
            // Fail-static: keep the last desired set, compute no diff, and
            // keep already-ready connections alive. While the store is
            // unreachable the loop cannot accept expanded desired sets, so
            // no new outbound connects are started after the failure.
            _storeFailureStartedAt ??= _time.GetTimestamp();
            _storeFailed = true;
            _localPublished = false;
            RetryPendingTargetsWithinStoreFailureGrace();
            return;
        }

        if (_storeFailed)
        {
            // First successful read after an outage: defer disconnects for
            // one heartbeat interval so other nodes get time to re-register.
            _storeFailed = false;
            _storeFailureStartedAt = null;
            _recoveryDeferUntil = _time.GetTimestamp()
                + (long)(_options.HeartbeatInterval.TotalSeconds * _time.TimestampFrequency);
        }

        var desired = ZLinkAutoConnectPlanner.ComputeDesired(_local, rows);
        Volatile.Write(
            ref _discoveredPeerCount,
            ZLinkAutoConnectPlanner.CountDiscoveredPeers(_local, rows));
        _lastDesired = new Dictionary<string, ZLinkAutoConnectTarget>(desired, StringComparer.Ordinal);
        // Membership snapshot for fail-fast target classification on the
        // send path (known peer vs unknown node). This is the full mesh
        // view, NOT the desired dial set: the pairwise initiator keeps
        // peers that dial us out of `desired`, yet they are reachable
        // rid-addressed targets. Fail-static: a store outage keeps the
        // last snapshot because the tick returns before this point.
        var members = new HashSet<string>(StringComparer.Ordinal);
        foreach (var row in rows)
        {
            if (row.NodeRid is { Size: > 0 } rowRid) members.Add(rowRid.ToHex());
        }

        _meshMemberRids = members;

        var connected = new List<string>();
        var disconnected = new List<string>();
        foreach (var (key, target) in desired)
        {
            if (!_active.TryGetValue(key, out var current))
            {
                if (_executor.Connect(target))
                {
                    _active[key] = target;
                    connected.Add(target.Endpoint);
                }
                continue;
            }

            if (RequiresConnectionHandover(current, target))
            {
                // An endpoint or connection-identity change needs a new
                // transport connection.
                if (!_executor.Disconnect(current)) continue;
                disconnected.Add(current.Endpoint);
                _active.Remove(key);
                if (_executor.Connect(target))
                {
                    _active[key] = target;
                    connected.Add(target.Endpoint);
                }
            }
            else if (OwnerChanged(current, target))
            {
                // A restarted process can reclaim the same endpoint under a new owner.
                // The transport already reconnects that broken endpoint. Tearing it down
                // again here races the reconnect and can leave a stale pipe beside the
                // replacement connection, so only refresh the reconciler's owner metadata.
                _active[key] = target;
            }
        }

        if (_time.GetTimestamp() >= _recoveryDeferUntil)
        {
            var toRemove = _active.Keys.Where(key => !desired.ContainsKey(key)).ToArray();
            foreach (var key in toRemove)
            {
                if (_executor.Disconnect(_active[key]))
                {
                    disconnected.Add(_active[key].Endpoint);
                    _active.Remove(key);
                }
            }
        }

        if (connected.Count > 0 || disconnected.Count > 0)
        {
            await _events.DesiredSetChangedAsync(
                new ZLinkAutoConnectDesiredSetChange(
                    _local.AutoConnectType, _local.MeshName, connected, disconnected),
                cancellationToken).ConfigureAwait(false);
        }
    }

    private static bool RequiresTargetRefresh(
        ZLinkAutoConnectTarget current,
        ZLinkAutoConnectTarget target) =>
        RequiresConnectionHandover(current, target) || OwnerChanged(current, target);

    private static bool RequiresConnectionHandover(
        ZLinkAutoConnectTarget current,
        ZLinkAutoConnectTarget target) =>
        !string.Equals(current.Endpoint, target.Endpoint, StringComparison.Ordinal)
        || !string.Equals(
            current.ConnectionFingerprint,
            target.ConnectionFingerprint,
            StringComparison.Ordinal);

    private static bool OwnerChanged(
        ZLinkAutoConnectTarget current,
        ZLinkAutoConnectTarget target) =>
        !string.Equals(current.OwnerId, target.OwnerId, StringComparison.Ordinal);

    private void RetryPendingTargetsWithinStoreFailureGrace()
    {
        if (_storeFailureStartedAt is not { } started
            || _options.StoreFailureGrace <= TimeSpan.Zero
            || _time.GetElapsedTime(started, _time.GetTimestamp()) > _options.StoreFailureGrace)
            return;

        foreach (var (key, target) in _lastDesired)
        {
            if (_active.ContainsKey(key)) continue;
            if (_executor.Connect(target)) _active[key] = target;
        }
    }

    internal async ValueTask ShutdownAsync(CancellationToken cancellationToken = default)
    {
        if (_localPublished)
        {
            await _runtime.RemovePeerAsync(LocalKey(), _localGeneration, cancellationToken)
                .ConfigureAwait(false);
            _localPublished = false;
        }

        foreach (var target in _active.Values)
        {
            _ = _executor.Disconnect(target);
        }

        _active.Clear();
    }

    private async ValueTask PublishLocalAsync(CancellationToken cancellationToken)
    {
        if (_localRow is null || _localPublished)
        {
            return;
        }

        // First claim uses NewClaim; when our previous row is still alive
        // (short outage, lease survived) the claim conflicts and a Renew
        // with the last generation extends it instead.
        var claim = await _runtime.WritePeerAsync(
            _localRow, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
            .ConfigureAwait(false);
        if (claim.Status == ZLinkLocationWriteStatus.Stored)
        {
            _localGeneration = claim.Generation;
            _localPublished = true;
            return;
        }

        if (claim.Status == ZLinkLocationWriteStatus.RejectedConflict && _localGeneration > 0)
        {
            var renewed = await _runtime.WritePeerAsync(
                _localRow with { Generation = _localGeneration },
                ZLinkLocationWriteIntent.Renew,
                cancellationToken).ConfigureAwait(false);
            _localPublished = renewed.Status == ZLinkLocationWriteStatus.Stored;
            return;
        }

        if (claim.Status == ZLinkLocationWriteStatus.RejectedConflict)
        {
            var takeover = await _runtime.WritePeerAsync(
                _localRow,
                ZLinkLocationWriteIntent.Takeover,
                cancellationToken).ConfigureAwait(false);
            if (takeover.Status == ZLinkLocationWriteStatus.Stored)
            {
                _localGeneration = takeover.Generation;
                _localPublished = true;
            }
        }
    }

    private ZLinkPeerLocationKey LocalKey() => new(
        _localRow!.AutoConnectType,
        _localRow.MeshName,
        _localRow.Role,
        _localRow.NodeRid,
        _localRow.Endpoint);
}
