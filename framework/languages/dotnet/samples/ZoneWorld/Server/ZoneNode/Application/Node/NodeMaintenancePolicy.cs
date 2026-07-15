using System.Collections.Concurrent;
using ZoneWorld.Server.Configuration;

namespace ZoneWorld.Server.ZoneNode.Application.Node;

/// <summary>
/// Maintenance mode (§2.3). The node judging a move is the one the player is leaving,
/// so it has to know the target node's state too. Reading the store on every move
/// would be expensive, so each node caches every node's state: seeded from the store
/// at startup and refreshed by the maintenance fanout.
///
/// The cache is an optimisation. Authority sits with the target node, which re-judges
/// on arrival and rejects the transfer if it is under maintenance — so a stale cache
/// costs a rejected move, never an admitted one.
/// </summary>
public sealed class NodeMaintenancePolicy(string ownNodeId)
{
    private readonly ConcurrentDictionary<string, bool> _byNode = new(StringComparer.Ordinal);

    public string OwnNodeId { get; } = ownNodeId;

    /// <summary>The authoritative answer for this node. No cache is involved.</summary>
    public bool IsOwnNodeUnderMaintenance => _byNode.TryGetValue(OwnNodeId, out var enabled) && enabled;

    public void Apply(string nodeId, bool enabled) => _byNode[nodeId] = enabled;

    /// <summary>
    /// Whether entering <paramref name="zoneId"/> is currently blocked. A move inside this
    /// node is never blocked, even while the node is under maintenance: maintenance only
    /// stops arrivals (§2.3).
    /// </summary>
    public bool ZoneIsUnreachable(string zoneId)
    {
        var targetNodeId = ZoneTopology.NodeOf(zoneId);
        if (targetNodeId == OwnNodeId) return false;
        return _byNode.TryGetValue(targetNodeId, out var enabled) && enabled;
    }

    public IReadOnlyDictionary<string, bool> Snapshot() =>
        _byNode.ToDictionary(entry => entry.Key, entry => entry.Value, StringComparer.Ordinal);
}
