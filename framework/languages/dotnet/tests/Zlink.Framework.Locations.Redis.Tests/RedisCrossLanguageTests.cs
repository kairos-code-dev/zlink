namespace Zlink.Framework.Locations.Redis.Tests;

public sealed class RedisCrossLanguageTests
{
    [SkippableFact]
    public async Task Dotnet_Reads_Node_Rows()
    {
        Skip.If(
            Environment.GetEnvironmentVariable("ZLINK_REDIS_TEST_ENDPOINT") is null
            || Environment.GetEnvironmentVariable(
                "ZLINK_REDIS_CROSS_LANGUAGE_PREFIX") is null,
            "Cross-language Redis harness environment is not configured.");
        await using var store = CreateStore("node");

        var actor = await store.ResolveActorAsync(new ZLinkActorLocationKey("node-actor"));
        Assert.NotNull(actor);
        Assert.Equal("node-actor", actor!.ActorRef.ActorId);
        Assert.Equal(RoutingId.From("node-node"), actor.ActorRef.NodeRid);
        Assert.Equal("node-owner", actor.OwnerId);

        var spot = await store.ResolveSpotAsync(new ZLinkSpotLocationKey("node-spot"));
        Assert.NotNull(spot);
        Assert.Equal("node-game", spot!.SpotType);
        Assert.Equal(RoutingId.From("node-node"), spot.OwnerNodeRid);

        var descriptors = await store.ListMeshNodesAsync("cross");
        var descriptor = Assert.Single(
            descriptors, row => row.Rid.Equals(RoutingId.From("node-node")));
        Assert.Equal("tcp://127.0.0.1:5320", descriptor.Endpoint);
        Assert.Equal(ZLinkFrameworkRuntimeState.Draining, descriptor.State);
        Assert.Equal(100, descriptor.ChannelWeights["cross"]);
    }

    [SkippableFact]
    public async Task Dotnet_Writes_Rows_For_Node_To_Read()
    {
        Skip.If(
            Environment.GetEnvironmentVariable("ZLINK_REDIS_TEST_ENDPOINT") is null
            || Environment.GetEnvironmentVariable(
                "ZLINK_REDIS_CROSS_LANGUAGE_PREFIX") is null,
            "Cross-language Redis harness environment is not configured.");
        const string ownerId = "dotnet-owner";
        var nodeRid = RoutingId.From("dotnet-node");
        await using var store = CreateStore("dotnet");
        await store.RenewOwnerLeaseAsync(ownerId, nodeRid, TimeSpan.FromSeconds(30));

        Assert.Equal(ZLinkLocationWriteStatus.Stored, (await store.UpdateActorAsync(new ZLinkActorLocation(
            "cross",
            "dotnet-actor",
            "player",
            new ActorRef(nodeRid, "dotnet-actor", 1),
            OwnerNodeRid: nodeRid,
            OwnerNodeGeneration: 1,
            SpotId: "dotnet-spot",
            SpotGeneration: 1,
            SpotKind: ZLinkSpotKind.User,
            MembershipEpoch: 1,
            OwnerId: ownerId,
            UpdatedAt: default), ZLinkLocationWriteIntent.NewClaim)).Status);

        Assert.Equal(ZLinkLocationWriteStatus.Stored, (await store.UpdateSpotAsync(new ZLinkSpotLocation(
            "cross",
            "dotnet-spot",
            SpotGeneration: 1,
            OwnerNodeRid: nodeRid,
            OwnerNodeGeneration: 1,
            SpotKind: ZLinkSpotKind.User,
            SpotType: "dotnet-game",
            OwnerId: ownerId,
            UpdatedAt: default), ZLinkLocationWriteIntent.NewClaim)).Status);

        Assert.Equal(ZLinkLocationWriteStatus.Stored, (await store.UpdateMeshNodeAsync(new ZLinkMeshNodeDescriptor(
            "cross",
            nodeRid,
            LifecycleGeneration: 1,
            DescriptorRevision: 1,
            "tcp://127.0.0.1:5310",
            new Dictionary<string, int>(StringComparer.Ordinal) { ["cross"] = 100 },
            SecurityIdentity: "cluster-a",
            OwnerId: ownerId,
            LeaseGeneration: 1,
            UpdatedAt: default)
        {
            State = ZLinkFrameworkRuntimeState.Draining
        }, ZLinkLocationWriteIntent.NewClaim)).Status);
    }

    private static ZLinkRedisLocationStore CreateStore(string suffix)
    {
        var endpoint = Environment.GetEnvironmentVariable("ZLINK_REDIS_TEST_ENDPOINT")
            ?? throw new InvalidOperationException("ZLINK_REDIS_TEST_ENDPOINT is required.");
        var prefix = Environment.GetEnvironmentVariable("ZLINK_REDIS_CROSS_LANGUAGE_PREFIX")
            ?? throw new InvalidOperationException("ZLINK_REDIS_CROSS_LANGUAGE_PREFIX is required.");
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions
        {
            ConnectionString = endpoint,
            KeyPrefix = $"{prefix}:{suffix}"
        });
    }
}
