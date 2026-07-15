namespace ZoneWorld.Shared.Contracts;

// The wire contract from scenario §7. The browser client mirrors these records in
// TypeScript; the field names and their meaning are the contract, so a rename here
// is a wire break for every language server and the client.

/// <summary>
/// ActorRef is a runtime type in every language, so it travels as a neutral DTO.
/// The receiver rebuilds its own ActorRef from these three values (§7.4).
/// </summary>
public sealed record ActorRefWire(string NodeRid, string ActorId, ulong Generation);

public sealed record PlayerView(string PlayerId, int X, int Y, string ZoneId, bool IsBot);

public sealed record NodeView(
    string NodeId,
    bool Registered,
    bool Connected,
    bool Maintenance,
    IReadOnlyList<string> Zones,
    int PlayerCount);

// ---------------------------------------------------------------------------
// §7.1 game — browser <-> Gateway (STREAM)
// ---------------------------------------------------------------------------

public sealed record JoinWorldReq(string PlayerId);

public sealed record JoinWorldRes(
    string PlayerId,
    string ZoneId,
    string NodeId,
    int X,
    int Y,
    string? Error = null);

public sealed record MoveMsg(int X, int Y);

public sealed record ZoneStateNotify(string ZoneId, long Tick, IReadOnlyList<PlayerView> Players);

public sealed record ZoneChangedNotify(string PlayerId, string ZoneId, string NodeId, bool Transferred);

public sealed record WorldAnnounceNotify(string AnnouncementId, string Text);

public sealed record MoveRejectedNotify(string Reason, int X, int Y);

// ---------------------------------------------------------------------------
// §7.2 ops console — browser <-> Ops (STREAM)
// ---------------------------------------------------------------------------

public sealed record WatchNodesReq;

public sealed record WatchNodesRes(IReadOnlyList<NodeView> Nodes);

public sealed record NodeStatusNotify(
    string NodeId,
    bool Registered,
    bool Connected,
    bool Maintenance,
    IReadOnlyList<string> Zones,
    int PlayerCount);

public sealed record NodeAlertNotify(string NodeId, string Kind, string Detail, string OccurredAt);

public sealed record AnnounceWorldReq(string Text);

public sealed record AnnounceWorldRes(string AnnouncementId);

public sealed record SetMaintenanceReq(string NodeId, bool Enabled);

public sealed record SetMaintenanceRes(
    string NodeId,
    bool Enabled,
    IReadOnlyList<string> Zones,
    string? Error = null);

public sealed record NodeDiagnosticsReq(string NodeId);

public sealed record NodeDiagnosticsRes(
    string NodeId,
    IReadOnlyList<string> Zones,
    int PlayerCount,
    bool Maintenance,
    string? Error = null);

// ---------------------------------------------------------------------------
// §7.3 server internal
// ---------------------------------------------------------------------------

/// <summary>Ops -> every ZoneNode (fanout `zoneworld.broadcast`, topic `world.announce`).</summary>
public sealed record WorldAnnounceEvent(string AnnouncementId, string Text);

/// <summary>Ops -> every ZoneNode (fanout, topic `world.maintenance`). Feeds the per-node cache (§2.3).</summary>
public sealed record NodeMaintenanceChangedEvent(string NodeId, bool Enabled);

/// <summary>Fanout subscriber -> the zone spots of its own node, through the spot bridge (§8.2).</summary>
public sealed record DeliverAnnounceMsg(string AnnouncementId, string Text);

/// <summary>
/// Zone spot -> bot actor. Drives one patrol step (§2.7). This is a request so the periodic
/// producer cannot build an unbounded movement backlog while the actor is transferring.
/// </summary>
public sealed record BotTickReq();

public sealed record BotTickRes();

/// <summary>
/// Gateway -> the ZoneNode that owns the spawn zone (channel `zoneworld.actors`).
/// Ensures the player actor exists and returns its ActorRef so the Gateway can bind
/// the session to it. The owning node is the authority for the maintenance
/// admission check (§2.3).
/// </summary>
public sealed record EnsurePlayerActorReq(string PlayerId);

public sealed record EnsurePlayerActorRes(string PlayerId, ActorRefWire Actor);

/// <summary>
/// Ensure handler -> the freshly created player actor, while it still sits in the entry
/// spot. The actor answers by joining its zone spot, which is the only way to enter a
/// zone (§2.6), and reports back where it landed. The caller has to wait for that join
/// before it can bind a session, which is why this is a request and not a send.
/// </summary>
public sealed record EnterWorldReq(int X, int Y, bool IsBot, int DirX = 0, int DirY = 0);

public sealed record EnterWorldRes(string ZoneId, string NodeId, int X, int Y, string? Error = null);

/// <summary>Ops -> one ZoneNode (owner-consistent channel `zoneworld.ops.&lt;NodeId&gt;`).</summary>
public sealed record ApplyNodeMaintenanceReq(string NodeId, bool Enabled);

public sealed record ApplyNodeMaintenanceRes(string NodeId, bool Enabled, IReadOnlyList<string> Zones);

public sealed record GetNodeDiagnosticsReq(string NodeId);

public sealed record GetNodeDiagnosticsRes(
    string NodeId,
    IReadOnlyList<string> Zones,
    int PlayerCount,
    bool Maintenance);

/// <summary>ZoneNode -> Ops (channel `zoneworld.report`). Sent when the event occurs (§8.1).</summary>
public sealed record ReportSpotEventMsg(string NodeId, string Kind, string Detail, string OccurredAt);

/// <summary>ZoneNode -> Ops (channel `zoneworld.report`). Sent every second (§8.1).</summary>
public sealed record ReportNodeStatusMsg(
    string NodeId,
    string NodeRid,
    IReadOnlyList<string> Zones,
    int PlayerCount,
    bool Maintenance);

/// <summary>
/// Zone spot -> adjacent zone spot (spot pub/sub, topic `zone.border.&lt;from&gt;.&lt;to&gt;`).
/// Loss is allowed; the receiver replaces per FromZoneId and expires stale snapshots (§2.4).
/// </summary>
public sealed record ZoneBorderEvent(
    string FromZoneId,
    string ToZoneId,
    long Tick,
    IReadOnlyList<PlayerView> Players);

/// <summary>
/// Player actor -> zone spot, as the JoinSpot admission payload. Joining is what
/// moves the actor between zones, and across nodes it is what triggers the actor
/// transfer, so the zone change rides the join rather than a plain send (§2.6).
/// The spot resolves the ActorRef from the actor directory instead of reading it
/// here: this payload is built on the source node, so an ActorRef carried in it
/// would be stale the moment the transfer lands (§8.3).
/// </summary>
/// <param name="FromNodeId">
/// The node the player is leaving, or null for a brand-new entry into the world. The
/// target spot is the authority on maintenance (§2.3) and it needs this to tell the two
/// cases apart: maintenance blocks arrivals from another node and blocks new entries,
/// but it does not block a player already on the node from moving between its zones.
/// The framework does not expose the source node to the admission callback, so the
/// payload carries it.
/// </param>
public sealed record EnterZoneMsg(
    string PlayerId,
    int X,
    int Y,
    bool IsBot,
    string? FromNodeId);

public sealed record EnterZoneRes(string ZoneId, string NodeId, string? Error = null);
