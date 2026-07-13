namespace Zlink.Framework.Locations.Redis.Tests;

public sealed class RedisCrossLanguageTests
{
    [Fact]
    public async Task Dotnet_Reads_Node_Rows()
    {
        await using var store = CreateStore("node");

        var actor = await store.ResolveActorAsync(new ZLinkActorLocationKey("node-actor"));
        Assert.NotNull(actor);
        Assert.Equal("node-actor", actor!.ActorRef?.ActorId);
        Assert.Equal(RoutingId.From("node-node"), actor.ActorRef?.NodeRid);
        Assert.Equal("node-owner", actor.OwnerId);

        var spot = await store.ResolveSpotAsync(new ZLinkSpotLocationKey("cross", RoutingId.From("node-spot")));
        Assert.NotNull(spot);
        Assert.Equal("node-game", spot!.SpotType);
        Assert.Equal(RoutingId.From("node-node"), spot.NodeRid);

        var route = await store.ResolveRouteAsync(new ZLinkRouteLocationKey(
            ZLinkRouteKind.ActorSession,
            "node-route"));
        Assert.NotNull(route);
        Assert.Equal(new byte[] { 5, 6, 7, 8 }, route!.Value.ToArray());

        var peers = await store.ListPeersAsync(new ZLinkPeerLocationFilter(
            AutoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
            MeshName: "cross",
            Role: ZLinkLocationRole.Router,
            NodeRid: RoutingId.From("node-node")));
        var peer = Assert.Single(peers);
        Assert.Equal("tcp://127.0.0.1:5320", peer.Endpoint);
        Assert.True(peer.Draining);
        Assert.Equal("tcp://127.0.0.1:6320", peer.Metadata!["route-endpoint"]);
        Assert.Equal(new[] { "node", "route" }, peer.Capabilities);
    }

    [Fact]
    public async Task Dotnet_Writes_Rows_For_Node_To_Read()
    {
        const string ownerId = "dotnet-owner";
        var nodeRid = RoutingId.From("dotnet-node");
        await using var store = CreateStore("dotnet");
        await store.RenewOwnerLeaseAsync(ownerId, nodeRid, TimeSpan.FromSeconds(30));

        Assert.Equal(ZLinkLocationWriteStatus.Stored, (await store.UpdateActorAsync(new ZLinkActorLocation(
            "dotnet-actor",
            "player",
            new ActorRef(nodeRid, "dotnet-actor", 1),
            nodeRid,
            ZLinkSpotKind.User,
            "cross",
            RoutingId.From("dotnet-spot"),
            ownerId,
            0,
            default), ZLinkLocationWriteIntent.NewClaim)).Status);

        Assert.Equal(ZLinkLocationWriteStatus.Stored, (await store.UpdateSpotAsync(new ZLinkSpotLocation(
            "cross",
            RoutingId.From("dotnet-spot"),
            "dotnet-game",
            nodeRid,
            ZLinkSpotKind.User,
            "tcp://127.0.0.1:5310",
            ownerId,
            0,
            default), ZLinkLocationWriteIntent.NewClaim)).Status);

        Assert.Equal(ZLinkLocationWriteStatus.Stored, (await store.UpdateRouteAsync(new ZLinkRouteLocation(
            ZLinkRouteKind.ActorSession,
            "dotnet-route",
            nodeRid,
            ownerId,
            0,
            new byte[] { 9, 8, 7, 6 },
            default), ZLinkLocationWriteIntent.NewClaim)).Status);

        Assert.Equal(ZLinkLocationWriteStatus.Stored, (await store.UpdatePeerAsync(new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.RouteMesh,
            "cross",
            nodeRid,
            ZLinkLocationRole.Router,
            "tcp://127.0.0.1:5310",
            100,
            true,
            11,
            new Dictionary<string, string> { ["route-endpoint"] = "tcp://127.0.0.1:6310" },
            ["dotnet", "route"],
            ownerId,
            0,
            default), ZLinkLocationWriteIntent.NewClaim)).Status);
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
