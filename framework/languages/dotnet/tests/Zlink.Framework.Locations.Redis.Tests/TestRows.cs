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
        SpotId: string.Empty,
        SpotGeneration: 0,
        SpotKind: ZLinkSpotKind.Entry,
        MembershipEpoch: 0,
        OwnerId: ownerId,
        UpdatedAt: default);

    internal static ZLinkSpotLocation Spot(
        string ownerId, string spotId, string meshName = "play") => new(
        meshName,
        spotId,
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
        string meshName = "play",
        long leaseGeneration = 1) => new(
        meshName,
        RoutingId.From(nodeRid),
        LifecycleGeneration: 1,
        DescriptorRevision: 1,
        endpoint,
        new Dictionary<string, int>(StringComparer.Ordinal) { [meshName] = 100, ["world"] = 50 },
        SecurityIdentity: "cluster-a",
        OwnerId: ownerId,
        LeaseGeneration: leaseGeneration,
        UpdatedAt: default)
    {
        ApplicationVersion = 7,
        ObjectRole = ZLinkMeshNodeObjectRole.Server,
        ObjectCapabilities =
        [
            new ZLinkObjectCapability(
                ZLinkPlacementObjectKind.Actor,
                "player",
                ZLinkObjectMaintenancePolicyKind.Recreate,
                HasSnapshotAdapter: false,
                Limit: 0)
        ],
        MaintenanceWave = "wave-a",
        State = ZLinkFrameworkRuntimeState.Serving,
        PlacementWeight = 80,
        Capacity = new(
            new ZLinkPopulationCapacity(12, 2, 1_000),
            new ZLinkPopulationCapacity(0, 0, 1_000),
            Array.Empty<ZLinkSpotTypeCapacity>()),
        EntrySpotId = EntrySpotId(meshName, nodeRid)
    };

    private static string EntrySpotId(string meshName, string nodeRid)
    {
        var hex = Convert.ToHexString(
                System.Security.Cryptography.SHA256.HashData(
                    System.Text.Encoding.UTF8.GetBytes(nodeRid)))
            .ToLowerInvariant();
        return $"{meshName}-entry-{hex[..8]}-{hex[8..12]}-4{hex[13..16]}"
               + $"-a{hex[17..20]}-{hex[20..32]}";
    }
}
