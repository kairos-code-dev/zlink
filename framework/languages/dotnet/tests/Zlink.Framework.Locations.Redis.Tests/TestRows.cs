namespace Zlink.Framework.Locations.Redis.Tests;

/// <summary>Row factories shared by the Redis integration tests and the
/// in-memory parity test. UpdatedAt is always store-issued; spot and
/// lifecycle generations are writer-carried spec fields.</summary>
internal static class TestRows
{
    internal static ZLinkActorLocation Actor(
        string ownerId, string actorId = "actor-1") => new(
        "play",
        actorId,
        "player",
        new ActorRef(RoutingId.From("node-1"), actorId, 1),
        OwnerNodeRid: RoutingId.From("node-1"),
        OwnerNodeGeneration: 0,
        SpotRid: default,
        SpotGeneration: 0,
        SpotKind: ZLinkSpotKind.Entry,
        MembershipEpoch: 0,
        OwnerId: ownerId,
        UpdatedAt: default);

    internal static ZLinkSpotLocation Spot(
        string ownerId, string spotRid, string meshName = "play") => new(
        meshName,
        RoutingId.From(spotRid),
        SpotGeneration: 0,
        OwnerNodeRid: RoutingId.From("node-1"),
        OwnerNodeGeneration: 0,
        SpotKind: ZLinkSpotKind.User,
        SpotType: "game",
        OwnerId: ownerId,
        UpdatedAt: default);

    internal static ZLinkMeshNodeDescriptor MeshNode(
        string ownerId,
        string endpoint = "tcp://127.0.0.1:5001",
        string nodeRid = "node-1",
        string meshName = "play") => new(
        meshName,
        RoutingId.From(nodeRid),
        LifecycleGeneration: 0,
        DescriptorRevision: 1,
        endpoint,
        new Dictionary<string, int>(StringComparer.Ordinal) { [meshName] = 100, ["world"] = 50 },
        Draining: false,
        SecurityIdentity: "cluster-a",
        OwnerId: ownerId,
        UpdatedAt: default);
}
