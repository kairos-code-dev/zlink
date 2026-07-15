using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Configuration;

/// <summary>
/// Which node hosts which zone. Every server needs this map: the ZoneNode to know
/// what to host, the Gateway to reach the node that owns the spawn zone, and the
/// Ops console to name a node's zones back to the operator.
/// </summary>
public static class ZoneTopology
{
    /// <summary>
    /// The zone placement the world spec fixes (§2): which node hosts which zones, and the
    /// routing id it answers to. One descriptor per node, so the node id, its routing id and
    /// its zones cannot drift apart — the runtime events carry the routing id, the operator
    /// speaks the node id, and Ops has to translate between them (§8.1).
    ///
    /// `zone-node-3` is deliberately absent. It hosts no zone, serves no channel, and nothing
    /// here or in Ops knows it exists — which is exactly what ZW-D2 proves: a node that was
    /// never configured anywhere still receives the fanout announcement (§11.1).
    /// </summary>
    private static readonly NodeDescriptor[] Nodes =
    [
        new(NodeIds.West, "zn1", [ZoneIds.NorthWest, ZoneIds.SouthWest]),
        new(NodeIds.East, "zn2", [ZoneIds.NorthEast, ZoneIds.SouthEast])
    ];

    /// <summary>The nodes that host zones. Not a discovery list: it is the §2 placement, which
    /// every role is entitled to know. Ops uses it to name the node the operator selected — the
    /// announcement path never touches it (§8.2).</summary>
    public static IReadOnlyList<string> ZoneNodes =>
        Nodes.Select(node => node.NodeId).ToArray();

    /// <summary>The routing id a node answers to on the spot mesh and the report channel.</summary>
    public static string RidOf(string nodeId) =>
        Find(nodeId)?.Rid
        ?? throw new ArgumentOutOfRangeException(nameof(nodeId), nodeId, "Not a zone node.");

    /// <summary>The node a routing id belongs to, or null when the id is not a zone node's —
    /// the Gateway shares the mesh and its events must not be read as a node's.</summary>
    public static string? NodeOfRid(string rid) =>
        Nodes.FirstOrDefault(node => node.Rid == rid)?.NodeId;

    /// <summary>The zones a node hosts — empty for a node that hosts none, which is what makes
    /// it a subscriber-only node (§11.1).</summary>
    public static IReadOnlyList<string> ZonesOf(string nodeId) =>
        Find(nodeId)?.Zones ?? [];

    public static string NodeOf(string zoneId) =>
        Nodes.FirstOrDefault(node => node.Zones.Contains(zoneId))?.NodeId
        ?? throw new ArgumentOutOfRangeException(nameof(zoneId), zoneId, "Unknown zone.");

    private static NodeDescriptor? Find(string nodeId) =>
        Nodes.FirstOrDefault(node => node.NodeId == nodeId);

    private sealed record NodeDescriptor(string NodeId, string Rid, IReadOnlyList<string> Zones);

    /// <summary>The zone a new player spawns into (§2, spawn coordinate is fixed).</summary>
    public static string SpawnZone => ZoneWorldSpec.ZoneOf(ZoneWorldSpec.SpawnX, ZoneWorldSpec.SpawnY);

    public static string SpawnNode => NodeOf(SpawnZone);

}
