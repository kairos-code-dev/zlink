using System.Runtime.CompilerServices;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Host;

/// <summary>
/// IZLinkRouteMeshRuntime over the registered MeshNodes (spec 50). Snapshots
/// read the Core status/peer tables directly; the event stream is derived by
/// polling those tables and emitting the spec identifiers for observed
/// transitions, so event handlers never sit on the dispatch path.
/// Core does not expose per-peer ChannelName sets, per-channel ready-member
/// counts, multicast admission counters or drain seal state yet; those
/// snapshot fields carry their empty values (gap 90 §12.37).
/// </summary>
internal sealed class ZLinkRouteMeshRuntimeService(
    ZLinkFrameworkRuntime runtime,
    ZLinkLocationStoreHealth? storeHealth,
    Func<IZLinkDrainControl?> drainControl) : IZLinkRouteMeshRuntime
{
    private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(100);

    private readonly object _sequenceGate = new();
    private readonly Dictionary<string, ulong> _sequences = new(StringComparer.Ordinal);

    public ZLinkMeshNodeSnapshot Snapshot(string meshName)
    {
        var nodeRuntime = runtime.GetMeshNodeRuntime(meshName);
        var status = nodeRuntime.Node.MeshStatus();
        var peers = nodeRuntime.Node.MeshPeers();
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
            peers.Select(MapPeer).ToArray(),
            MapChannels(nodeRuntime, peers),
            new ZLinkLogicalMulticastSnapshot(
                nodeRuntime.Registration.SpotPublisherConfig.NoDrop,
                status.MulticastSubmitted,
                Backpressured: 0,
                status.MulticastDroppedTargets,
                RemoteSnapshotCount: 0,
                RemoteAdmittedCount: 0,
                RemoteDroppedCount: 0,
                LocalSnapshotCount: 0,
                LocalAdmittedCount: 0,
                LocalDroppedCount: 0,
                PendingAdmissionCount: 0),
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

    public async IAsyncEnumerable<ZLinkMeshRuntimeEvent> ObserveAsync(
        string meshName,
        int capacity = 1024,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        var nodeRuntime = runtime.GetMeshNodeRuntime(meshName);
        MeshNodeState? lastState = null;
        var lastPeers = new Dictionary<RoutingId, MeshNodePeer>();
        while (!cancellationToken.IsCancellationRequested)
        {
            var status = nodeRuntime.Node.MeshStatus();
            var peers = nodeRuntime.Node.MeshPeers();

            if (lastState != status.State)
            {
                lastState = status.State;
                yield return Event(
                    "zlink.runtime.mesh_node.state_changed",
                    meshName,
                    status.RoutingId,
                    state: MapNodeState(status.State));
            }

            var seen = new HashSet<RoutingId>();
            foreach (var peer in peers)
            {
                seen.Add(peer.RoutingId);
                if (lastPeers.TryGetValue(peer.RoutingId, out var previous)
                    && previous.State == peer.State
                    && previous.LifecycleGeneration == peer.LifecycleGeneration
                    && previous.DescriptorRevision == peer.DescriptorRevision)
                    continue;

                lastPeers[peer.RoutingId] = peer;
                yield return Event(
                    "zlink.runtime.mesh_node.peer_changed",
                    meshName,
                    status.RoutingId,
                    peerRid: peer.RoutingId,
                    lifecycleGeneration: peer.LifecycleGeneration,
                    descriptorRevision: peer.DescriptorRevision,
                    reason: MapPeerState(peer).AdmissionState);
            }

            foreach (var gone in lastPeers.Keys.Where(rid => !seen.Contains(rid)).ToArray())
            {
                lastPeers.Remove(gone);
                yield return Event(
                    "zlink.runtime.mesh_node.peer_changed",
                    meshName,
                    status.RoutingId,
                    peerRid: gone,
                    reason: "disconnected");
            }

            try
            {
                await Task.Delay(PollInterval, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                yield break;
            }
        }
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

    private static ZLinkMeshDrainResult MapDrainResult(ZLinkDrainResult result)
    {
        return result switch
        {
            Drained => new ZLinkMeshDrainResult.Drained(),
            ForceStopped forced => new ZLinkMeshDrainResult.ForceStopped(forced.Reason.ToString()),
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
        if (storeHealth is null)
            return new ZLinkLocationRuntimeSnapshot("not_configured", null, null);

        var snapshot = storeHealth.GetSnapshot();
        return new ZLinkLocationRuntimeSnapshot(
            snapshot.Healthy ? "ready" : "degraded",
            snapshot.LastSuccessAt,
            LastFailureAt: null);
    }

    private static ZLinkMeshPeerSnapshot MapPeer(MeshNodePeer peer)
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
            ChannelNames: [],
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
        IReadOnlyList<MeshNodePeer> peers)
    {
        var readyPeers = peers.Count(static peer => peer.State == MeshPeerState.Admitted);
        return nodeRuntime.Registration.ChannelMemberships
            .Select(membership =>
            {
                var readyMembers = readyPeers + (membership.Weight > 0 ? 1 : 0);
                return new ZLinkMeshChannelSnapshot(
                    membership.ChannelName,
                    membership.Weight,
                    readyMembers,
                    readyMembers > 0);
            })
            .ToArray();
    }

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
}
