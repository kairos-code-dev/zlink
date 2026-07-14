using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Configuration;

/// <summary>
/// Which node hosts which zone. Every server needs this map: the ZoneNode to know
/// what to host, the Gateway to reach the node that owns the spawn zone, and the
/// Ops console to name a node's zones back to the operator.
/// </summary>
public static class ZoneTopology
{
    private static readonly Dictionary<string, string> ZoneToNode = new(StringComparer.Ordinal)
    {
        [ZoneIds.NorthWest] = NodeIds.West,
        [ZoneIds.SouthWest] = NodeIds.West,
        [ZoneIds.NorthEast] = NodeIds.East,
        [ZoneIds.SouthEast] = NodeIds.East
    };

    /// <summary>
    /// Every node, in order, with the routing id it answers to. This is the one place the two
    /// names for a node are tied together: the runtime events carry the routing id, the operator
    /// speaks the node id, and Ops has to translate between them (§8.1). Adding a node is a
    /// change here and nowhere else.
    /// </summary>
    private static readonly (string NodeId, string Rid)[] Nodes =
    [
        (NodeIds.West, "zn1"),
        (NodeIds.East, "zn2"),
        (NodeIds.Extra, "zn3")
    ];

    public static IReadOnlyList<string> AllNodes =>
        Nodes.Select(node => node.NodeId).ToArray();

    /// <summary>The routing id a node answers to on the spot mesh and the report channel.</summary>
    public static string RidOf(string nodeId) =>
        Nodes.FirstOrDefault(node => node.NodeId == nodeId).Rid
        ?? throw new ArgumentOutOfRangeException(nameof(nodeId), nodeId, "Unknown node.");

    /// <summary>The node a routing id belongs to, or null when the id is not a zone node's —
    /// the Gateway shares the mesh and its events must not be read as a node's.</summary>
    public static string? NodeOfRid(string rid) =>
        Nodes.FirstOrDefault(node => node.Rid == rid).NodeId;

    public static IReadOnlyList<string> ZonesOf(string nodeId) =>
        ZoneToNode.Where(entry => entry.Value == nodeId)
            .Select(entry => entry.Key)
            .OrderBy(zone => zone, StringComparer.Ordinal)
            .ToArray();

    public static string NodeOf(string zoneId) =>
        ZoneToNode.TryGetValue(zoneId, out var nodeId)
            ? nodeId
            : throw new ArgumentOutOfRangeException(nameof(zoneId), zoneId, "Unknown zone.");

    /// <summary>The zone a new player spawns into (§2, spawn coordinate is fixed).</summary>
    public static string SpawnZone => ZoneOf(ZoneWorldSpec.SpawnX, ZoneWorldSpec.SpawnY);

    public static string SpawnNode => NodeOf(SpawnZone);

    /// <summary>
    /// The zone containing a coordinate. This duplicates no rule: the ZoneNode domain
    /// owns movement, but the topology needs the same split to place a spawn.
    /// </summary>
    public static string ZoneOf(int x, int y)
    {
        var west = x < ZoneWorldSpec.ZoneSplit;
        var north = y < ZoneWorldSpec.ZoneSplit;
        return (west, north) switch
        {
            (true, true) => ZoneIds.NorthWest,
            (false, true) => ZoneIds.NorthEast,
            (true, false) => ZoneIds.SouthWest,
            (false, false) => ZoneIds.SouthEast
        };
    }
}
