using System.Runtime.CompilerServices;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Host;

/// <summary>
/// IZLinkRouteMeshRuntime over the registered MeshNodes (spec 50). Snapshots
/// read the Core status/peer tables directly; the event stream consumes the
/// independent Core MeshNode monitor, so event handlers never sit on the
/// dispatch path.
/// Peer ChannelName sets and channel readiness are derived from the Core peer
/// and peer-channel snapshots. Core does not expose all multicast admission
/// counters or drain seal state yet; those fields retain their gap values.
/// </summary>
internal sealed class ZLinkRouteMeshRuntimeService(
    ZLinkFrameworkRuntime runtime,
    ZLinkLocationStoreHealth? storeHealth,
    IZLinkLocationRuntimeQuery? locationQuery,
    Func<IZLinkDrainControl?> drainControl) : IZLinkRouteMeshRuntime, IDisposable
{
    private static readonly TimeSpan MonitorIdleDelay = TimeSpan.FromMilliseconds(10);

    private readonly ZLinkLocationStoreHealth? _storeHealth = storeHealth;
    private readonly IZLinkLocationRuntimeQuery? _locationQuery = locationQuery;
    private readonly object _sequenceGate = new();
    private readonly Dictionary<string, ulong> _sequences = new(StringComparer.Ordinal);
    private readonly object _monitorGate = new();
    private readonly Dictionary<string, MonitorHub> _monitorHubs =
        new(StringComparer.Ordinal);
    private bool _stopped;

    public ZLinkMeshNodeSnapshot Snapshot(string meshName)
    {
        var nodeRuntime = runtime.GetMeshNodeRuntime(meshName);
        var hub = GetOrCreateHub(meshName, nodeRuntime);
        var status = nodeRuntime.Node.MeshStatus();
        var monitorStatus = hub.Status();
        var peers = nodeRuntime.Node.MeshPeers();
        var peerChannels = peers.Select(peer =>
                SnapshotPeerChannels(nodeRuntime, peer))
            .ToArray();
        var targetCounts = hub.TargetCounts();
        var state = MapNodeState(status.State);
        return new ZLinkMeshNodeSnapshot(
            meshName,
            status.RoutingId,
            status.LifecycleGeneration,
            status.DescriptorRevision,
            status.LocalEndpoint,
            state,
            NextSequence(meshName),
            DateTimeOffset.UtcNow,
            DescriptorSources(nodeRuntime),
            peers.Select((peer, index) => MapPeer(peer, peerChannels[index]))
                .ToArray(),
            MapChannels(nodeRuntime, peers, peerChannels),
            new ZLinkLogicalMulticastSnapshot(
                status.MulticastSubmitted,
                monitorStatus.BackpressuredSubmits,
                status.MulticastDroppedTargets,
                targetCounts.RemoteSnapshot,
                targetCounts.RemoteAdmitted,
                targetCounts.RemoteDropped,
                targetCounts.LocalSnapshot,
                targetCounts.LocalAdmitted,
                targetCounts.LocalDropped),
            new ZLinkMeshClaimSnapshot(
                ApplicationActive: state == ZLinkMeshNodeState.Serving,
                status.PendingApplicationMessages,
                InfrastructureActive: state is ZLinkMeshNodeState.Serving or ZLinkMeshNodeState.Draining,
                status.PendingInfrastructureMessages),
            LocationSnapshot(),
            new ZLinkMeshDrainSnapshot(
                state,
                Deadline: null,
                WorkSealed: state is ZLinkMeshNodeState.Drained or ZLinkMeshNodeState.Stopped,
                status.PendingApplicationMessages,
                PendingTransferCount: 0,
                PendingStreamBarrierCount: 0));
    }

    internal void Start()
    {
        foreach (var meshName in runtime.Registration.SpotNodes.Keys)
        {
            var nodeRuntime = runtime.GetMeshNodeRuntime(meshName);
            _ = GetOrCreateHub(meshName, nodeRuntime);
        }
    }

    internal void Stop()
    {
        MonitorHub[] hubs;
        lock (_monitorGate)
        {
            if (_stopped)
                return;
            _stopped = true;
            hubs = [.. _monitorHubs.Values];
            _monitorHubs.Clear();
        }
        foreach (var hub in hubs)
            hub.Stop();
    }

    public void Dispose() => Stop();

    private MonitorHub GetOrCreateHub(
        string meshName,
        ZLinkSpotNodeRuntime nodeRuntime)
    {
        lock (_monitorGate)
        {
            if (_monitorHubs.TryGetValue(meshName, out var hub))
                return hub;
            if (_stopped)
                throw new ObjectDisposedException(nameof(ZLinkRouteMeshRuntimeService));
            hub = new MonitorHub(this, meshName, nodeRuntime);
            _monitorHubs.Add(meshName, hub);
            hub.Start();
            return hub;
        }
    }

    public async IAsyncEnumerable<ZLinkMeshRuntimeEvent> ObserveAsync(
        string meshName,
        int capacity = 1024,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        if (capacity <= 0)
            throw new ArgumentOutOfRangeException(nameof(capacity));

        var (hub, observer) = SubscribeMonitor(meshName, capacity);
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var next = await observer.ReadAsync(cancellationToken)
                    .ConfigureAwait(false);
                if (next is null)
                    yield break;
                yield return next;
            }
        }
        finally
        {
            UnsubscribeMonitor(meshName, hub, observer);
        }
    }

    private (MonitorHub Hub, ObserverQueue Observer) SubscribeMonitor(
        string meshName,
        int capacity)
    {
        var nodeRuntime = runtime.GetMeshNodeRuntime(meshName);
        var hub = GetOrCreateHub(meshName, nodeRuntime);
        return (hub, hub.Subscribe(capacity));
    }

    private void UnsubscribeMonitor(
        string meshName,
        MonitorHub hub,
        ObserverQueue observer)
    {
        lock (_monitorGate)
        {
            _ = meshName;
            hub.Unsubscribe(observer);
        }
    }

    private IReadOnlyList<ZLinkMeshRuntimeEvent> MapMonitorEvents(
        string meshName,
        RoutingId sourceRid,
        MeshMonitorEvent nativeEvent)
    {
        RoutingId? peerRid = nativeEvent.PeerRid.IsEmpty
            ? null
            : nativeEvent.PeerRid;
        var identifier = nativeEvent.Kind switch
        {
            MeshMonitorEventKind.StateChanged =>
                "zlink.runtime.mesh_node.state_changed",
            MeshMonitorEventKind.ChannelChanged =>
                "zlink.runtime.mesh_node.channel_changed",
            MeshMonitorEventKind.Backpressured =>
                "zlink.runtime.mesh_node.multicast_backpressured",
            MeshMonitorEventKind.MulticastDropped =>
                "zlink.runtime.mesh_node.multicast_dropped",
            MeshMonitorEventKind.ClaimRevoked =>
                "zlink.runtime.mesh_node.claim_changed",
            MeshMonitorEventKind.PeerConnecting
                or MeshMonitorEventKind.PeerAdmitted
                or MeshMonitorEventKind.PeerDraining
                or MeshMonitorEventKind.PeerClosed
                or MeshMonitorEventKind.PeerRejected
                or MeshMonitorEventKind.ProtocolError =>
                "zlink.runtime.mesh_node.peer_changed",
            _ => null
        };
        if (identifier is null)
            return [];

        var reason = nativeEvent.Kind switch
        {
            MeshMonitorEventKind.PeerConnecting => "connecting",
            MeshMonitorEventKind.PeerAdmitted => "ready",
            MeshMonitorEventKind.PeerDraining => "draining",
            MeshMonitorEventKind.PeerClosed => "disconnected",
            MeshMonitorEventKind.PeerRejected => "HandshakeFailed",
            MeshMonitorEventKind.ProtocolError => "rejected",
            MeshMonitorEventKind.Backpressured => "backpressure",
            _ => null
        };
        var carriesTargetCounts = nativeEvent.Kind is
            MeshMonitorEventKind.MulticastCommitted
            or MeshMonitorEventKind.MulticastDropped
            or MeshMonitorEventKind.Backpressured;
        var mapped = new ZLinkMeshRuntimeEvent(
            identifier,
            NextSequence(meshName),
            DateTimeOffset.UtcNow,
            meshName,
            sourceRid,
            peerRid,
            nativeEvent.PeerLifecycleGeneration == 0
                ? null
                : nativeEvent.PeerLifecycleGeneration,
            nativeEvent.PeerDescriptorRevision == 0
                ? null
                : nativeEvent.PeerDescriptorRevision,
            string.IsNullOrEmpty(nativeEvent.ChannelName)
                ? null
                : nativeEvent.ChannelName,
            ClaimDomain: nativeEvent.Kind == MeshMonitorEventKind.ClaimRevoked
                ? "application"
                : null,
            MessageKind: null,
            carriesTargetCounts ? nativeEvent.SnapshotRemoteTargetCount : null,
            carriesTargetCounts ? nativeEvent.AdmittedRemoteTargetCount : null,
            carriesTargetCounts ? nativeEvent.DroppedRemoteTargetCount : null,
            carriesTargetCounts ? nativeEvent.SnapshotLocalSpotCount : null,
            carriesTargetCounts ? nativeEvent.AdmittedLocalSpotCount : null,
            carriesTargetCounts ? nativeEvent.DroppedLocalSpotCount : null,
            reason,
            nativeEvent.Kind == MeshMonitorEventKind.StateChanged
                ? MapNodeState(nativeEvent.MeshState)
                : null);
        if (nativeEvent.Kind != MeshMonitorEventKind.StateChanged)
            return [mapped];

        // A Core lifecycle transition changes both the general node state and
        // the drain snapshot. Framework exposes both stable identifiers.
        return
        [
            mapped,
            mapped with
            {
                Identifier = "zlink.runtime.mesh_node.drain_changed",
                Sequence = NextSequence(meshName)
            }
        ];
    }

    public bool IsReady(string meshName)
    {
        return runtime.GetMeshNodeRuntime(meshName).Node.MeshStatus().State
            is MeshNodeState.Started or MeshNodeState.PartialReady or MeshNodeState.Ready;
    }

    public async ValueTask<ZLinkMeshDrainResult> DrainAsync(
        string meshName,
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default)
    {
        EnsureHostDrainMatchesMeshScope(meshName);
        _ = runtime.GetMeshNodeRuntime(meshName);
        var control = RequireDrainControl();
        var result = deadline is { } fixedDeadline
            ? await control.DrainAsync(fixedDeadline, cancellationToken).ConfigureAwait(false)
            : await control.DrainAsync(TimeSpan.FromSeconds(30), cancellationToken).ConfigureAwait(false);
        return MapDrainResult(result);
    }

    public async ValueTask<ZLinkMeshDrainResult> AwaitDrainedAsync(
        string meshName,
        CancellationToken cancellationToken = default)
    {
        EnsureHostDrainMatchesMeshScope(meshName);
        _ = runtime.GetMeshNodeRuntime(meshName);
        var control = RequireDrainControl();
        return MapDrainResult(await control.AwaitDrainedAsync(cancellationToken).ConfigureAwait(false));
    }

    private IZLinkDrainControl RequireDrainControl()
    {
        return drainControl()
               ?? throw new ZLinkConfigurationException(
                   "Graceful drain requires the framework host drain control.");
    }

    private void EnsureHostDrainMatchesMeshScope(string meshName)
    {
        if (!runtime.Registration.SpotNodes.ContainsKey(meshName))
            throw new ZLinkConfigurationException(
                $"RouteMesh '{meshName}' is not registered.");
        if (runtime.Registration.SpotNodes.Count != 1)
            throw new ZLinkConfigurationException(
                "Mesh-scoped drain is not available when the host contains multiple RouteMesh registrations.");
    }

    private static ZLinkMeshDrainResult MapDrainResult(ZLinkDrainResult result)
    {
        return result switch
        {
            Drained => new ZLinkMeshDrainResult.Drained(),
            ForceStopped forced => new ZLinkMeshDrainResult.ForceStopped(
                forced.Reason switch
                {
                    ZLinkDrainForceReason.DeadlineExceeded => "deadline_exceeded",
                    ZLinkDrainForceReason.DrainingStatePublishFailed => "drain_state_publish_failed",
                    ZLinkDrainForceReason.OwnerCleanupFailed => "owner_cleanup_failed",
                    ZLinkDrainForceReason.TeardownFailed => "teardown_failed",
                    _ => throw new InvalidOperationException(
                        $"Unknown drain force reason '{forced.Reason}'.")
                }),
            _ => throw new InvalidOperationException(
                $"Unknown drain result '{result.GetType().Name}'.")
        };
    }

    private ulong NextSequence(string meshName)
    {
        lock (_sequenceGate)
        {
            var next = _sequences.TryGetValue(meshName, out var current) ? current + 1 : 1;
            _sequences[meshName] = next;
            return next;
        }
    }

    private ZLinkMeshRuntimeEvent Event(
        string identifier,
        string meshName,
        RoutingId sourceRid,
        RoutingId? peerRid = null,
        ulong? lifecycleGeneration = null,
        ulong? descriptorRevision = null,
        string? channelName = null,
        string? reason = null,
        ZLinkMeshNodeState? state = null)
    {
        return new ZLinkMeshRuntimeEvent(
            identifier,
            NextSequence(meshName),
            DateTimeOffset.UtcNow,
            meshName,
            sourceRid,
            peerRid,
            lifecycleGeneration,
            descriptorRevision,
            channelName,
            ClaimDomain: null,
            MessageKind: null,
            RemoteSnapshotCount: null,
            RemoteAdmittedCount: null,
            RemoteDroppedCount: null,
            LocalSnapshotCount: null,
            LocalAdmittedCount: null,
            LocalDroppedCount: null,
            reason,
            state);
    }

    private static IReadOnlyList<string> DescriptorSources(ZLinkSpotNodeRuntime nodeRuntime)
    {
        var manual = nodeRuntime.Registration.Router?.ManualConnections.Count > 0;
        var redis = nodeRuntime.Registration.Router?.AcquisitionMode
            == ZLinkPeerAcquisitionMode.AutoConnect;
        return manual && redis
            ? ["manual_and_redis"]
            : manual
                ? ["manual"]
                : redis
                    ? ["redis"]
                    : [];
    }

    private ZLinkLocationRuntimeSnapshot LocationSnapshot()
    {
        if (_storeHealth is null)
            return new ZLinkLocationRuntimeSnapshot("not_configured", null, null);

        var snapshot = _storeHealth.GetSnapshot();
        return new ZLinkLocationRuntimeSnapshot(
            snapshot.Healthy ? "ready" : "degraded",
            snapshot.LastSuccessAt,
            snapshot.LastFailureAt);
    }

    private static IReadOnlyList<MeshPeerChannel> SnapshotPeerChannels(
        ZLinkSpotNodeRuntime nodeRuntime,
        MeshNodePeer peer)
    {
        if (peer.State == MeshPeerState.Closed || peer.RoutingId.IsEmpty)
            return [];
        try
        {
            return nodeRuntime.Node.MeshPeerChannels(
                peer.RoutingId,
                peer.LifecycleGeneration);
        }
        catch (ZlinkConfigException error)
            when (error.Result == ZlinkConfigException.ErrorCode.NotFound)
        {
            // Peers and their channel table are separate atomic Core reads.
            // A lifecycle that ended between them is no longer selectable.
            return [];
        }
    }

    private static ZLinkMeshPeerSnapshot MapPeer(
        MeshNodePeer peer,
        IReadOnlyList<MeshPeerChannel> channels)
    {
        var mapped = MapPeerState(peer);
        return new ZLinkMeshPeerSnapshot(
            peer.RoutingId,
            peer.LifecycleGeneration,
            peer.DescriptorRevision,
            peer.Endpoint,
            mapped.AdmissionState,
            mapped.Ready,
            mapped.DrainState,
            channels.Select(static channel => channel.Name).ToArray(),
            peer.LastError == 0 ? null : $"errno {peer.LastError}");
    }

    private static (string AdmissionState, bool Ready, string DrainState) MapPeerState(
        MeshNodePeer peer)
    {
        return peer.State switch
        {
            MeshPeerState.Configured => ("configured", false, "serving"),
            MeshPeerState.Connecting => ("connecting", false, "serving"),
            MeshPeerState.Admitted => ("ready", true, "serving"),
            MeshPeerState.Draining => ("draining", false, "draining"),
            MeshPeerState.Closed => ("disconnected", false, "serving"),
            _ => ("rejected", false, "serving")
        };
    }

    private static IReadOnlyList<ZLinkMeshChannelSnapshot> MapChannels(
        ZLinkSpotNodeRuntime nodeRuntime,
        IReadOnlyList<MeshNodePeer> peers,
        IReadOnlyList<MeshPeerChannel>[] peerChannels)
    {
        var localMemberships = nodeRuntime.Registration.ChannelMemberships
            .ToDictionary(
                static membership => membership.ChannelName,
                static membership => membership.Weight,
                StringComparer.Ordinal);
        var channelNames = localMemberships.Keys
            .Concat(peerChannels.SelectMany(static channels =>
                channels.Select(static channel => channel.Name)))
            .Distinct(StringComparer.Ordinal)
            .OrderBy(static channelName => channelName, StringComparer.Ordinal);

        return channelNames
            .Select(channelName =>
            {
                var localWeight = localMemberships.GetValueOrDefault(channelName);
                var readyMembers = localWeight > 0 ? 1 : 0;
                for (var index = 0; index < peers.Count; index++)
                {
                    if (peers[index].State != MeshPeerState.Admitted)
                        continue;
                    if (peerChannels[index].Any(channel =>
                            channel.Weight > 0
                            && string.Equals(
                                channel.Name,
                                channelName,
                                StringComparison.Ordinal)))
                        readyMembers++;
                }
                return new ZLinkMeshChannelSnapshot(
                    channelName,
                    localWeight,
                    readyMembers,
                    readyMembers > 0);
            })
            .ToArray();
    }

    private readonly record struct MulticastTargetCounts(
        ulong RemoteSnapshot,
        ulong RemoteAdmitted,
        ulong RemoteDropped,
        ulong LocalSnapshot,
        ulong LocalAdmitted,
        ulong LocalDropped);

    private static ZLinkMeshNodeState MapNodeState(MeshNodeState state)
    {
        return state switch
        {
            MeshNodeState.Created => ZLinkMeshNodeState.Starting,
            MeshNodeState.Started or MeshNodeState.PartialReady or MeshNodeState.Ready =>
                ZLinkMeshNodeState.Serving,
            MeshNodeState.Draining => ZLinkMeshNodeState.Draining,
            MeshNodeState.Stopped => ZLinkMeshNodeState.Stopped,
            _ => ZLinkMeshNodeState.Faulted
        };
    }

    /// <summary>
    /// Owns Core's single monitor for one MeshNode and fans events out without
    /// putting observer backpressure on the native receive loop.
    /// </summary>
    private sealed class MonitorHub
    {
        private readonly ZLinkRouteMeshRuntimeService _owner;
        private readonly string _meshName;
        private readonly ZLinkSpotNodeRuntime _nodeRuntime;
        private readonly IMeshNodeMonitor _monitor;
        private readonly CancellationTokenSource _stop = new();
        private Task? _pump;
        private readonly object _gate = new();
        private readonly List<ObserverQueue> _observers = [];
        private readonly Dictionary<RoutingId, ZLinkMeshNodeDescriptor> _descriptors = [];
        private DateTimeOffset _nextDescriptorPoll;
        private MulticastTargetCounts _targetCounts;
        private string? _lastLocationState;

        public MonitorHub(
            ZLinkRouteMeshRuntimeService owner,
            string meshName,
            ZLinkSpotNodeRuntime nodeRuntime)
        {
            _owner = owner;
            _meshName = meshName;
            _nodeRuntime = nodeRuntime;
            _monitor = nodeRuntime.Node.OpenMeshMonitor();
        }

        public void Start() => _pump ??= Task.Run(PumpAsync);

        public MeshMonitorStatus Status() => _monitor.Status();

        public MulticastTargetCounts TargetCounts()
        {
            lock (_gate)
                return _targetCounts;
        }

        public ObserverQueue Subscribe(int capacity)
        {
            var status = _nodeRuntime.Node.MeshStatus();
            var observer = new ObserverQueue(capacity);
            observer.Enqueue(_owner.Event(
                "zlink.runtime.mesh_node.state_changed",
                _meshName,
                status.RoutingId,
                state: MapNodeState(status.State)));
            lock (_gate)
                _observers.Add(observer);
            return observer;
        }

        public void Unsubscribe(ObserverQueue observer)
        {
            lock (_gate)
            {
                _observers.Remove(observer);
            }
            observer.Complete();
        }

        public void Stop()
        {
            _stop.Cancel();
            _pump?.GetAwaiter().GetResult();
            _stop.Dispose();
        }

        private async Task PumpAsync()
        {
            try
            {
                while (!_stop.IsCancellationRequested)
                {
                    var nativeEvent = _monitor.Recv(RecvFlags.DontWait);
                    if (nativeEvent is null)
                    {
                        try
                        {
                            await PublishDescriptorChangesAsync(_stop.Token)
                                .ConfigureAwait(false);
                        }
                        catch (OperationCanceledException) when (_stop.IsCancellationRequested)
                        {
                            throw;
                        }
                        catch
                        {
                            // Location health owns store-failure reporting.
                            // Monitoring remains subscribed and retries the
                            // snapshot on the next polling interval.
                        }
                        await Task.Delay(MonitorIdleDelay, _stop.Token)
                            .ConfigureAwait(false);
                        continue;
                    }

                    RecordTargetCounts(nativeEvent);
                    var sourceRid = _nodeRuntime.Node.MeshStatus().RoutingId;
                    var mapped = _owner.MapMonitorEvents(
                        _meshName, sourceRid, nativeEvent);
                    ObserverQueue[] observers;
                    lock (_gate)
                        observers = [.. _observers];
                    foreach (var runtimeEvent in mapped)
                    foreach (var observer in observers)
                        observer.Enqueue(runtimeEvent);
                }
            }
            catch (OperationCanceledException) when (_stop.IsCancellationRequested)
            {
            }
            catch (Exception)
            {
                ObserverQueue[] observers;
                lock (_gate)
                {
                    observers = [.. _observers];
                    _observers.Clear();
                }
                foreach (var observer in observers)
                    observer.Complete();
            }
            finally
            {
                _monitor.Dispose();
            }
        }

        private async ValueTask PublishDescriptorChangesAsync(
            CancellationToken cancellationToken)
        {
            PublishLocationHealthChange();
            if (_owner._locationQuery is null || DateTimeOffset.UtcNow < _nextDescriptorPoll)
                return;
            _nextDescriptorPoll = DateTimeOffset.UtcNow.AddMilliseconds(100);

            var current = await _owner._locationQuery.ListMeshNodeDescriptorsAsync(
                    _meshName,
                    cancellationToken)
                .ConfigureAwait(false);
            if (_descriptors.Count == 0)
            {
                foreach (var descriptor in current)
                    _descriptors[descriptor.Rid] = descriptor;
                return;
            }

            var sourceRid = _nodeRuntime.Node.MeshStatus().RoutingId;
            foreach (var descriptor in current)
            {
                if (!_descriptors.TryGetValue(descriptor.Rid, out var previous))
                {
                    _descriptors[descriptor.Rid] = descriptor;
                    continue;
                }
                if (previous.DescriptorRevision == descriptor.DescriptorRevision
                    && previous.ChannelWeights.Count == descriptor.ChannelWeights.Count
                    && previous.ChannelWeights.All(pair =>
                        descriptor.ChannelWeights.TryGetValue(pair.Key, out var weight)
                        && weight == pair.Value))
                    continue;

                _descriptors[descriptor.Rid] = descriptor;
                foreach (var channelName in previous.ChannelWeights.Keys
                             .Concat(descriptor.ChannelWeights.Keys)
                             .Distinct(StringComparer.Ordinal))
                {
                    previous.ChannelWeights.TryGetValue(channelName, out var oldWeight);
                    descriptor.ChannelWeights.TryGetValue(channelName, out var newWeight);
                    if (oldWeight == newWeight)
                        continue;
                    Publish(_owner.Event(
                        "zlink.runtime.mesh_node.channel_changed",
                        _meshName,
                        sourceRid,
                        peerRid: descriptor.Rid,
                        descriptorRevision: descriptor.DescriptorRevision,
                        channelName: channelName,
                        reason: "admission_changed"));
                }
            }
        }

        private void PublishLocationHealthChange()
        {
            if (_owner._storeHealth is null)
                return;
            var state = _owner._storeHealth.GetSnapshot().Healthy ? "ready" : "degraded";
            if (_lastLocationState is null)
            {
                _lastLocationState = state;
                return;
            }
            if (string.Equals(_lastLocationState, state, StringComparison.Ordinal))
                return;
            _lastLocationState = state;
            var sourceRid = _nodeRuntime.Node.MeshStatus().RoutingId;
            Publish(_owner.Event(
                "zlink.runtime.location.store_changed",
                _meshName,
                sourceRid,
                reason: state));
        }

        private void RecordTargetCounts(MeshMonitorEvent nativeEvent)
        {
            var isTerminalTargetEvent = nativeEvent.Kind
                is MeshMonitorEventKind.MulticastCommitted
                or MeshMonitorEventKind.MulticastDropped
                or MeshMonitorEventKind.Backpressured;
            if (!isTerminalTargetEvent)
                return;

            // A publish with remote backpressure and a local drop emits both
            // BACKPRESSURED and MULTICAST_DROPPED with the same target grid.
            // The first event owns the cumulative snapshot contribution.
            if (nativeEvent.Kind == MeshMonitorEventKind.MulticastDropped
                && nativeEvent.ResultCode
                == (int)ZlinkSubmitException.ErrorCode.Backpressured
                && nativeEvent.DroppedRemoteTargetCount > 0)
                return;

            lock (_gate)
            {
                _targetCounts = new MulticastTargetCounts(
                    _targetCounts.RemoteSnapshot
                    + nativeEvent.SnapshotRemoteTargetCount,
                    _targetCounts.RemoteAdmitted
                    + nativeEvent.AdmittedRemoteTargetCount,
                    _targetCounts.RemoteDropped
                    + nativeEvent.DroppedRemoteTargetCount,
                    _targetCounts.LocalSnapshot
                    + nativeEvent.SnapshotLocalSpotCount,
                    _targetCounts.LocalAdmitted
                    + nativeEvent.AdmittedLocalSpotCount,
                    _targetCounts.LocalDropped
                    + nativeEvent.DroppedLocalSpotCount);
            }
        }

        private void Publish(ZLinkMeshRuntimeEvent runtimeEvent)
        {
            ObserverQueue[] observers;
            lock (_gate)
                observers = [.. _observers];
            foreach (var observer in observers)
                observer.Enqueue(runtimeEvent);
        }
    }

    /// <summary>
    /// Per-observer bounded queue. Counter events are aggregated on overflow,
    /// state events retain the newest value, and terminal drain events cannot
    /// be displaced by later non-terminal traffic.
    /// </summary>
    private sealed class ObserverQueue
    {
        private readonly int _capacity;
        private readonly object _gate = new();
        private readonly List<ZLinkMeshRuntimeEvent> _pending = [];
        private readonly SemaphoreSlim _available = new(0);
        private bool _completed;

        public ObserverQueue(int capacity)
        {
            _capacity = capacity;
        }

        public void Enqueue(ZLinkMeshRuntimeEvent runtimeEvent)
        {
            lock (_gate)
            {
                if (_completed)
                    return;
                if (_pending.Count == _capacity)
                {
                    var aggregateIndex = IsCounterEvent(runtimeEvent)
                        ? _pending.FindIndex(item =>
                            item.Identifier == runtimeEvent.Identifier)
                        : -1;
                    if (aggregateIndex >= 0)
                    {
                        _pending[aggregateIndex] = MergeCounters(
                            _pending[aggregateIndex], runtimeEvent);
                        return;
                    }

                    var terminalIndex = _pending.FindIndex(IsTerminalDrainEvent);
                    if (terminalIndex >= 0 && !IsTerminalDrainEvent(runtimeEvent))
                        return;

                    var removableIndex = IsTerminalDrainEvent(runtimeEvent)
                        ? _pending.FindIndex(item => !IsTerminalDrainEvent(item))
                        : 0;
                    if (removableIndex < 0)
                        removableIndex = 0;
                    _pending.RemoveAt(removableIndex);
                    _pending.Add(runtimeEvent);
                    return;
                }

                _pending.Add(runtimeEvent);
                _available.Release();
            }
        }

        public async ValueTask<ZLinkMeshRuntimeEvent?> ReadAsync(
            CancellationToken cancellationToken)
        {
            try
            {
                await _available.WaitAsync(cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return null;
            }

            lock (_gate)
            {
                if (_pending.Count == 0)
                    return null;
                var next = _pending[0];
                _pending.RemoveAt(0);
                return next;
            }
        }

        public void Complete()
        {
            lock (_gate)
            {
                if (_completed)
                    return;
                _completed = true;
                _available.Release();
            }
        }

        private static bool IsCounterEvent(ZLinkMeshRuntimeEvent runtimeEvent)
        {
            return runtimeEvent.Identifier is
                "zlink.runtime.mesh_node.multicast_backpressured"
                or "zlink.runtime.mesh_node.multicast_dropped";
        }

        private static bool IsTerminalDrainEvent(
            ZLinkMeshRuntimeEvent runtimeEvent)
        {
            return runtimeEvent.Identifier ==
                   "zlink.runtime.mesh_node.drain_changed"
                   && runtimeEvent.State is ZLinkMeshNodeState.Drained
                       or ZLinkMeshNodeState.Stopped
                       or ZLinkMeshNodeState.ForceStopping;
        }

        private static ZLinkMeshRuntimeEvent MergeCounters(
            ZLinkMeshRuntimeEvent older,
            ZLinkMeshRuntimeEvent newer)
        {
            return newer with
            {
                RemoteSnapshotCount = Sum(
                    older.RemoteSnapshotCount, newer.RemoteSnapshotCount),
                RemoteAdmittedCount = Sum(
                    older.RemoteAdmittedCount, newer.RemoteAdmittedCount),
                RemoteDroppedCount = Sum(
                    older.RemoteDroppedCount, newer.RemoteDroppedCount),
                LocalSnapshotCount = Sum(
                    older.LocalSnapshotCount, newer.LocalSnapshotCount),
                LocalAdmittedCount = Sum(
                    older.LocalAdmittedCount, newer.LocalAdmittedCount),
                LocalDroppedCount = Sum(
                    older.LocalDroppedCount, newer.LocalDroppedCount)
            };
        }

        private static ulong? Sum(ulong? left, ulong? right)
        {
            return left is null && right is null
                ? null
                : (left ?? 0) + (right ?? 0);
        }
    }
}
