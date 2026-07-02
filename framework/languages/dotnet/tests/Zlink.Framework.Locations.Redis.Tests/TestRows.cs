namespace Zlink.Framework.Locations.Redis.Tests;

/// <summary>Row factories shared by the Redis integration tests and the
/// in-memory parity test. Generation and UpdatedAt are always store-issued,
/// so callers pass the generation only where the Renew guard needs it.</summary>
internal static class TestRows
{
    internal static ZLinkActorLocation Actor(
        string ownerId, long generation, string actorId = "actor-1") => new(
        "player",
        actorId,
        "actor-ref",
        RoutingId.From("node-1"),
        generation,
        ZLinkSpotKind.Entry,
        null,
        ZLinkSpotKind.Entry,
        ownerId,
        default);

    internal static ZLinkSpotLocation Spot(
        string ownerId, string spotRid, string meshName = "play") => new(
        meshName,
        RoutingId.From(spotRid),
        "game",
        RoutingId.From("node-1"),
        ZLinkSpotKind.User,
        null,
        ownerId,
        0,
        default);

    internal static ZLinkPeerLocation Peer(
        string ownerId,
        string endpoint = "tcp://127.0.0.1:5001",
        string nodeRid = "node-1") => new(
        ZLinkLocationAutoConnectType.RouteMesh,
        "play",
        RoutingId.From(nodeRid),
        ZLinkLocationRole.Router,
        endpoint,
        100,
        7,
        new Dictionary<string, string> { ["route-endpoint"] = "tcp://127.0.0.1:6001" },
        ["router", "route-bridge"],
        ownerId,
        0,
        default);

    internal static ZLinkRouteLocation Route(
        string ownerId, string routeKey = "route-1", long generation = 0) => new(
        ZLinkRouteKind.ActorSession,
        routeKey,
        RoutingId.From("node-1"),
        ownerId,
        generation,
        new byte[] { 1, 2, 3, 4 },
        default);
}
