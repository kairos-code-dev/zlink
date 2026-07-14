namespace ZoneWorld.Shared.Contracts;

/// <summary>
/// The framework element names fixed by scenario §4. The name choices carry the
/// topology decision, so they live next to the wire contract rather than in each
/// server's configuration.
/// </summary>
public static class ZoneWorldNames
{
    /// <summary>Spot mesh hosting the zone spots and the player actors.</summary>
    public const string ZoneMesh = "zoneworld.zones";

    /// <summary>
    /// Route mesh registered only so the runtime can build the spot bridge that
    /// carries a channel handler's message into a zone spot in the same process.
    /// It is never used to address a node from the application (§1.1).
    /// </summary>
    public const string BridgeMesh = "zoneworld.bridge";

    /// <summary>Fanout channel: Ops publishes, every ZoneNode subscribes.</summary>
    public const string BroadcastChannel = "zoneworld.broadcast";

    /// <summary>Client-server channel: ZoneNode reports to Ops.</summary>
    public const string ReportChannel = "zoneworld.report";

    /// <summary>Client-server channel: Gateway ensures a player actor on a ZoneNode.</summary>
    public const string ActorsChannel = "zoneworld.actors";

    /// <summary>
    /// Owner-consistent channel. Each ZoneNode serves the channel named after
    /// itself, so a call reaches that node and no other (§8.4).
    /// </summary>
    public static string OpsChannel(string nodeId) => $"zoneworld.ops.{nodeId}";

    public const string AnnounceTopic = "world.announce";
    public const string MaintenanceTopic = "world.maintenance";

    /// <summary>Zone spots publish the border band per adjacent zone (§4.1).</summary>
    public static string BorderTopic(string fromZoneId, string toZoneId) =>
        $"zone.border.{fromZoneId}.{toZoneId}";

    public const string PlayerActorType = "zoneworld.player";

    public const string GatewayStreamNode = "zoneworld.gateway";
    public const string OpsStreamNode = "zoneworld.ops";

    /// <summary>
    /// Monitoring source names (§8.1). A socket source names a channel and the capability
    /// on it; a spot source names the SpotNode. They are not free-form labels — the runtime
    /// resolves them against what was actually registered.
    /// </summary>
    public const string OpsLocationSource = "zoneworld.ops.location";
    public const string OpsSocketSource = ReportChannel + ".server";
    public const string ZoneSpotSource = ZoneMesh;
}

public static class HandlerGroups
{
    public const string ZoneNode = "zone-node";
    public const string Ops = "ops";
}
