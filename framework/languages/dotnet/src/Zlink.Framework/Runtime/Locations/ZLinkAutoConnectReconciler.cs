namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Executes the connect/disconnect decisions of the reconciler. The channel
/// runtime implements this over core socket calls; stores never touch
/// sockets. Connect failures are the executor's concern (retry backoff) and
/// are never a reason to remove a location row.
/// </summary>
internal interface IZLinkAutoConnectExecutor
{
    void Connect(ZLinkAutoConnectTarget target);

    void Disconnect(ZLinkAutoConnectTarget target);
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
    private readonly ZLinkPeerLocation? _localRow;
    private readonly ZLinkLocationRuntime _runtime;
    private readonly IZLinkPeerLocationResolver _peers;
    private readonly IZLinkAutoConnectExecutor _executor;
    private readonly ZLinkLocationOptions _options;
    private readonly ZLinkLocationEventEmitter _events;
    private readonly TimeProvider _time;
    private readonly Dictionary<string, ZLinkAutoConnectTarget> _active = new(StringComparer.Ordinal);
    private long _localGeneration;
    private bool _localPublished;
    private bool _storeFailed;
    private long _recoveryDeferUntil;

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

    /// <summary>True while the last tick could not read the store. The loop
    /// must not let a change stamp skip ticks in this state.</summary>
    internal bool StoreFailed => _storeFailed;

    internal async ValueTask TickAsync(CancellationToken cancellationToken = default)
    {
        // Publish (or re-publish after recovery) the local row before
        // reading the list, so peers observing the store during our
        // recovery window can already see us.
        await PublishLocalAsync(cancellationToken).ConfigureAwait(false);

        IReadOnlyList<ZLinkPeerLocation> rows;
        try
        {
            rows = await _peers.ListPeersAsync(
                new ZLinkPeerLocationFilter(
                    AutoConnectType: _local.AutoConnectType,
                    MeshName: _local.MeshName),
                ZLinkResolveFreshness.Refresh,
                cancellationToken).ConfigureAwait(false);
        }
        catch (Exception)
        {
            // Fail-static: keep the last desired set, compute no diff, and
            // let already-ready connections live until transport failure.
            _storeFailed = true;
            _localPublished = false;
            return;
        }

        if (_storeFailed)
        {
            // First successful read after an outage: defer disconnects for
            // one heartbeat interval so other nodes get time to re-register.
            _storeFailed = false;
            _recoveryDeferUntil = _time.GetTimestamp()
                + (long)(_options.HeartbeatInterval.TotalSeconds * _time.TimestampFrequency);
        }

        var desired = ZLinkAutoConnectPlanner.ComputeDesired(_local, rows);

        var connected = new List<string>();
        var disconnected = new List<string>();
        foreach (var (key, target) in desired)
        {
            if (!_active.TryGetValue(key, out var current))
            {
                _executor.Connect(target);
                _active[key] = target;
                connected.Add(target.Endpoint);
                continue;
            }

            if (!string.Equals(current.Endpoint, target.Endpoint, StringComparison.Ordinal))
            {
                // Same peer key with a new endpoint is a handover: replace
                // the old connection with the new endpoint.
                _executor.Disconnect(current);
                _executor.Connect(target);
                _active[key] = target;
                disconnected.Add(current.Endpoint);
                connected.Add(target.Endpoint);
            }
        }

        if (_time.GetTimestamp() >= _recoveryDeferUntil)
        {
            var toRemove = _active.Keys.Where(key => !desired.ContainsKey(key)).ToArray();
            foreach (var key in toRemove)
            {
                _executor.Disconnect(_active[key]);
                disconnected.Add(_active[key].Endpoint);
                _active.Remove(key);
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
            _executor.Disconnect(target);
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
        }
    }

    private ZLinkPeerLocationKey LocalKey() => new(
        _localRow!.AutoConnectType,
        _localRow.MeshName,
        _localRow.Role,
        _localRow.NodeRid,
        _localRow.Endpoint);
}
