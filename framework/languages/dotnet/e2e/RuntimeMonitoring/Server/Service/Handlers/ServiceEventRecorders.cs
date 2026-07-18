using RuntimeMonitoring.Server.Service.Support;
using Zlink.Framework.Contracts.Eventing;

namespace RuntimeMonitoring.Server.Service.Handlers;

internal sealed class SocketEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"monitor-socket|source={@event.SourceName}|kind={@event.Event}"
            + $"|remote={@event.RemoteAddr}|routing={@event.RoutingId}"
            + $"|native={@event.Diagnostic?.NativeEvent}|value={@event.Diagnostic?.NativeValue}");
        return ValueTask.CompletedTask;
    }
}

// Mesh runtime events (spec 50) replace the 9.x socket sources for the mesh
// plane: a peer reaching ready is the wire-level connection identity and a
// peer leaving is its disconnect. The advertised endpoint comes from the
// mesh snapshot (events carry rid+generation only) and is cached so the
// disconnect line still names the endpoint of a gone peer.
internal sealed class MeshEventRecorder(
    EvidenceStore evidence,
    Zlink.Framework.Contracts.Configuration.IZLinkRouteMeshRuntime meshRuntime)
    : IZLinkRuntimeEventHandler<Zlink.Framework.Contracts.Configuration.ZLinkMeshRuntimeEvent>
{
    private static readonly System.Collections.Concurrent.ConcurrentDictionary<string, string>
        LastKnownEndpoints = new(StringComparer.Ordinal);

    public ValueTask HandleAsync(
        Zlink.Framework.Contracts.Configuration.ZLinkMeshRuntimeEvent @event,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var peer = @event.PeerRid?.ToString() ?? string.Empty;
        var endpoint = string.Empty;
        if (peer.Length > 0)
        {
            try
            {
                endpoint = meshRuntime.Snapshot(@event.MeshName).Peers
                    .FirstOrDefault(entry => entry.Rid.ToString() == peer)?.Endpoint ?? string.Empty;
            }
            catch (Exception)
            {
                // The mesh may be stopping; the cached endpoint still names it.
            }

            if (endpoint.Length > 0) LastKnownEndpoints[peer] = endpoint;
            else LastKnownEndpoints.TryGetValue(peer, out endpoint!);
        }

        var kind = @event.Reason switch
        {
            "ready" => "ConnectionReady",
            "disconnected" => "Disconnected",
            _ => @event.Identifier == "zlink.runtime.mesh_node.state_changed"
                ? $"State:{@event.State}"
                : @event.Reason ?? @event.Identifier
        };
        evidence.Add(
            $"monitor-mesh|source={@event.MeshName}|kind={kind}"
            + $"|remote={endpoint}|routing={peer}|sequence={@event.Sequence}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class ThrowingSocketEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"monitor-throw|source={@event.SourceName}|kind={@event.Event}");
        throw new InvalidOperationException("monitoring dispatch failure for e2e");
    }
}

internal sealed class LocationRuntimeEventRecorder(
    EvidenceStore evidence,
    LocationTopologyTransitionTracker transitions)
    : IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>
{
    public ValueTask HandleAsync(ZLinkLocationRuntimeEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var topologyCount = @event is ZLinkLocationRuntimeEvent.TopologyChanged topology
            ? topology.Topology.Count
            : -1;
        var summaryCount = @event is ZLinkLocationRuntimeEvent.ServiceSummaryChanged summary
            ? summary.ServiceSummary.Count
            : -1;
        var topologyEntries = @event is ZLinkLocationRuntimeEvent.TopologyChanged changed
            ? changed.Topology
                .Select(entry => $"{entry.NodeRid}:{entry.State}")
                .Distinct(StringComparer.Ordinal)
                .Order(StringComparer.Ordinal)
                .ToArray()
            : [];
        var summaryEntries = @event is ZLinkLocationRuntimeEvent.ServiceSummaryChanged summaryChanged
            ? summaryChanged.ServiceSummary
                .Select(entry => $"{entry.MeshName}:"
                                 + $"{entry.TotalCount}:{entry.ReadyCount}:{entry.ErrorCount}:{entry.StoppedCount}")
                .Order(StringComparer.Ordinal)
                .ToArray()
            : [];
        var transition = @event is ZLinkLocationRuntimeEvent.TopologyChanged topologyChanged
            ? transitions.Apply(topologyChanged.Topology)
            : default;
        evidence.Add(
            $"monitor-location-runtime|source={@event.SourceName}|kind={@event.GetType().Name}"
            + $"|topology={topologyCount}|summary={summaryCount}"
            + $"|entries={string.Join(',', topologyEntries)}"
            + $"|summary-entries={string.Join(',', summaryEntries)}"
            + $"|added={string.Join(',', transition.Added ?? [])}"
            + $"|removed={string.Join(',', transition.Removed ?? [])}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class LocationTopologyTransitionTracker
{
    private readonly object _gate = new();
    private HashSet<string> _nodes = new(StringComparer.Ordinal);

    public (string[] Added, string[] Removed) Apply(
        IReadOnlyList<Zlink.Framework.Contracts.Locations.ZLinkLocationTopologyEntry> topology)
    {
        var current = topology
            .Where(entry => entry.State == Zlink.Framework.Contracts.Locations.ZLinkLocationTopologyState.Ready)
            .Select(entry => entry.NodeRid.ToString())
            .Where(static nodeRid => !nodeRid.StartsWith("hex:", StringComparison.Ordinal))
            .ToHashSet(StringComparer.Ordinal);
        lock (_gate)
        {
            var added = current.Except(_nodes, StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
            var removed = _nodes.Except(current, StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
            _nodes = current;
            return (added, removed);
        }
    }
}

internal sealed class SpotEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
{
    public ValueTask HandleAsync(ZLinkSpotEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var peerCount = @event is ZLinkSpotEvent.PeersChanged peers ? peers.Peers.Count : -1;
        var subjectCount = @event is ZLinkSpotEvent.SubjectsChanged subjects ? subjects.Subjects.Count : -1;
        var subjectNames = @event is ZLinkSpotEvent.SubjectsChanged subjectChange
            ? subjectChange.Subjects.Select(subject => subject.Subject).Order(StringComparer.Ordinal).ToArray()
            : [];
        var timerName = @event switch
        {
            ZLinkSpotEvent.TimerHandlerFailed failed => failed.Diagnostic.TimerName,
            ZLinkSpotEvent.TimerStoppedAfterUnhandledException stopped => stopped.Diagnostic.TimerName,
            _ => "<null>"
        };
        evidence.Add(
            $"monitor-spot|source={@event.SourceName}|kind={@event.GetType().Name}"
            + $"|peers={peerCount}|subjects={subjectCount}"
            + $"|subject-names={string.Join(',', subjectNames)}"
            + $"|timer={timerName}");
        return ValueTask.CompletedTask;
    }
}
