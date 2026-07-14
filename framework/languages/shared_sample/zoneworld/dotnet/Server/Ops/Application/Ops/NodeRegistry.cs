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
    private readonly ConcurrentDictionary<string, NodeView> _nodes = new(StringComparer.Ordinal);

    public event Action<NodeView>? Changed;

    public IReadOnlyList<NodeView> Snapshot() =>
        _nodes.Values
            .OrderBy(node => node.NodeId, StringComparer.Ordinal)
            .ToArray();

    public void ApplyRegistration(string nodeId, bool registered) =>
        Update(nodeId, node => node with { Registered = registered });

    public void ApplyConnection(string nodeId, bool connected) =>
        Update(nodeId, node => node with { Connected = connected });

    public void ApplyReport(ReportNodeStatusMsg report) =>
        Update(report.NodeId, node => node with
        {
            Zones = report.Zones,
            PlayerCount = report.PlayerCount,
            Maintenance = report.Maintenance,
            Registered = true
        });

    private void Update(string nodeId, Func<NodeView, NodeView> change)
    {
        var updated = _nodes.AddOrUpdate(
            nodeId,
            _ => change(Empty(nodeId)),
            (_, existing) => change(existing));

        Changed?.Invoke(updated);
    }

    private static NodeView Empty(string nodeId) =>
        new(nodeId, Registered: false, Connected: false, Maintenance: false, Zones: [], PlayerCount: 0);
}
