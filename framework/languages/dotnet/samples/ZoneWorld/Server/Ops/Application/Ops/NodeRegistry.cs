using System.Collections.Concurrent;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Application.Ops;

/// <summary>
/// What the console knows about each node. Two sources feed it and neither is enough
/// alone (§8.1): the runtime events Ops can observe in its own process say whether a node
/// is registered and connected, and the node's own report says which zones it hosts, how
/// many players it holds and whether it is under maintenance.
/// </summary>
public sealed class NodeRegistry
{
    private readonly ConcurrentDictionary<string, NodeState> _nodes = new(StringComparer.Ordinal);

    public event Action<NodeView>? Changed;

    public IReadOnlyList<NodeView> Snapshot() =>
        _nodes.Values
            .Select(state => state.View)
            .OrderBy(node => node.NodeId, StringComparer.Ordinal)
            .ToArray();

    public void ApplyRegistration(string nodeId, bool registered) =>
        Update(nodeId, state => state with
        {
            View = state.View with
            {
                Registered = registered,
                Connected = registered && state.TransportConnected
            }
        });

    public void ApplyConnection(string nodeId, bool connected) =>
        Update(nodeId, state => state with
        {
            TransportConnected = connected,
            // A reconnecting socket can emit ConnectionReady after its remote node has already
            // left the location topology. Keep that transport observation, but do not present
            // an unavailable node to the console as connected.
            View = state.View with { Connected = state.View.Registered && connected }
        });

    public void ApplyReport(ReportNodeStatusMsg report) =>
        Update(report.NodeId, state => state with
        {
            View = state.View with
            {
                Zones = report.Zones,
                PlayerCount = report.PlayerCount,
                Maintenance = report.Maintenance
            }
        });

    private void Update(string nodeId, Func<NodeState, NodeState> change)
    {
        var updated = _nodes.AddOrUpdate(
            nodeId,
            _ => change(Empty(nodeId)),
            (_, existing) => change(existing));

        Changed?.Invoke(updated.View);
    }

    private static NodeState Empty(string nodeId) =>
        new(
            new NodeView(
                nodeId,
                Registered: false,
                Connected: false,
                Maintenance: false,
                Zones: [],
                PlayerCount: 0),
            TransportConnected: false);

    private sealed record NodeState(NodeView View, bool TransportConnected);
}
