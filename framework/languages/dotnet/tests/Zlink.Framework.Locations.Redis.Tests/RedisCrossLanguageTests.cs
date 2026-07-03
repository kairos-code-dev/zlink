namespace Zlink.Framework.Locations.Redis.Tests;

[Collection(RedisTestCollection.Name)]
public sealed class RedisCrossLanguageTests
{
    private readonly RedisTestFixture _fixture;

    public RedisCrossLanguageTests(RedisTestFixture fixture)
    {
        _fixture = fixture;
    }

    [SkippableFact]
    public async Task Dotnet_Reads_Java_Rows()
    {
        await using var store = CreateCrossLanguageStore("java");

        var actor = await store.ResolveActorAsync(new ZLinkActorLocationKey("player", "java-actor"));
        var spot = await store.ResolveSpotAsync(new ZLinkSpotLocationKey("cross", RoutingId.From("java-spot")));
        var route = await store.ResolveRouteAsync(new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, "java-route"));
        var peers = await store.ListPeersAsync(new ZLinkPeerLocationFilter(
            AutoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
            MeshName: "cross",
            Role: ZLinkLocationRole.Router,
            NodeRid: RoutingId.From("java-node")));

        Assert.NotNull(actor);
        Assert.Equal("java-ref", actor!.ActorRef);
        Assert.Equal(RoutingId.From("java-node"), actor.NodeRid);
        Assert.Equal("java-owner", actor.OwnerId);

        Assert.NotNull(spot);
        Assert.Equal("java-game", spot!.SpotType);
        Assert.Equal(RoutingId.From("java-node"), spot.NodeRid);

        Assert.NotNull(route);
        Assert.Equal(new byte[] { 1, 2, 3, 4 }, route!.Value.ToArray());

        var peer = Assert.Single(peers.Where(row => row.Endpoint == "tcp://127.0.0.1:5300"));
        Assert.Equal("tcp://127.0.0.1:6300", peer.Metadata!["route-endpoint"]);
        Assert.Equal(["java", "route"], peer.Capabilities);
    }

    [SkippableFact]
    public async Task Dotnet_Writes_Rows_For_Java_To_Read()
    {
        await using var store = CreateCrossLanguageStore("dotnet");

        await store.RenewOwnerLeaseAsync(
            "dotnet-owner",
            RoutingId.From("dotnet-node"),
            TimeSpan.FromSeconds(30));

        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdatePeerAsync(DotnetPeer(), ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdateSpotAsync(DotnetSpot(), ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdateActorAsync(DotnetActor(), ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdateRouteAsync(DotnetRoute(), ZLinkLocationWriteIntent.NewClaim)).Status);
    }

    [SkippableFact]
    public async Task Dotnet_Reads_Node_Rows()
    {
        await using var store = CreateCrossLanguageStore("node");

        var actor = await store.ResolveActorAsync(new ZLinkActorLocationKey("player", "node-actor"));
        var spot = await store.ResolveSpotAsync(new ZLinkSpotLocationKey("cross", RoutingId.From("node-spot")));
        var route = await store.ResolveRouteAsync(new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, "node-route"));
        var peers = await store.ListPeersAsync(new ZLinkPeerLocationFilter(
            AutoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
            MeshName: "cross",
            Role: ZLinkLocationRole.Router,
            NodeRid: RoutingId.From("node-node")));

        Assert.NotNull(actor);
        Assert.Equal("node-ref", actor!.ActorRef);
        Assert.Equal(RoutingId.From("node-node"), actor.NodeRid);
        Assert.Equal("node-owner", actor.OwnerId);

        Assert.NotNull(spot);
        Assert.Equal("node-game", spot!.SpotType);
        Assert.Equal(RoutingId.From("node-node"), spot.NodeRid);

        Assert.NotNull(route);
        Assert.Equal(new byte[] { 5, 6, 7, 8 }, route!.Value.ToArray());

        var peer = Assert.Single(peers.Where(row => row.Endpoint == "tcp://127.0.0.1:5320"));
        Assert.Equal("tcp://127.0.0.1:6320", peer.Metadata!["route-endpoint"]);
        Assert.Equal(["node", "route"], peer.Capabilities);
    }

    [SkippableFact]
    public async Task Dotnet_Writes_Rows_For_Node_To_Read()
    {
        await using var store = CreateCrossLanguageStore("dotnet");

        await store.RenewOwnerLeaseAsync(
            "dotnet-owner",
            RoutingId.From("dotnet-node"),
            TimeSpan.FromSeconds(30));

        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdatePeerAsync(DotnetPeer(), ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdateSpotAsync(DotnetSpot(), ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdateActorAsync(DotnetActor(), ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdateRouteAsync(DotnetRoute(), ZLinkLocationWriteIntent.NewClaim)).Status);
    }

    [SkippableFact]
    public async Task Dotnet_Reads_Cpp_Rows()
    {
        await using var store = CreateCrossLanguageStore("cpp");

        var actor = await store.ResolveActorAsync(new ZLinkActorLocationKey("player", "cpp-actor"));
        var spot = await store.ResolveSpotAsync(new ZLinkSpotLocationKey("cross", RoutingId.From("cpp-spot")));
        var route = await store.ResolveRouteAsync(new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, "cpp-route"));
        var peers = await store.ListPeersAsync(new ZLinkPeerLocationFilter(
            AutoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
            MeshName: "cross",
            Role: ZLinkLocationRole.Router,
            NodeRid: RoutingId.From("cpp-node")));

        Assert.NotNull(actor);
        Assert.Equal("cpp-ref", actor!.ActorRef);
        Assert.Equal(RoutingId.From("cpp-node"), actor.NodeRid);
        Assert.Equal("cpp-owner", actor.OwnerId);

        Assert.NotNull(spot);
        Assert.Equal("cpp-game", spot!.SpotType);
        Assert.Equal(RoutingId.From("cpp-node"), spot.NodeRid);

        Assert.NotNull(route);
        Assert.Equal(new byte[] { 10, 11, 12, 13 }, route!.Value.ToArray());

        var peer = Assert.Single(peers.Where(row => row.Endpoint == "tcp://127.0.0.1:5330"));
        Assert.Equal("tcp://127.0.0.1:6330", peer.Metadata!["route-endpoint"]);
        Assert.Equal(["cpp", "route"], peer.Capabilities);
    }

    [SkippableFact]
    public async Task Dotnet_Writes_Rows_For_Cpp_To_Read()
    {
        await using var store = CreateCrossLanguageStore("dotnet");

        await store.RenewOwnerLeaseAsync(
            "dotnet-owner",
            RoutingId.From("dotnet-node"),
            TimeSpan.FromSeconds(30));

        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdatePeerAsync(DotnetPeer(), ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdateSpotAsync(DotnetSpot(), ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdateActorAsync(DotnetActor(), ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(ZLinkLocationWriteStatus.Stored,
            (await store.UpdateRouteAsync(DotnetRoute(), ZLinkLocationWriteIntent.NewClaim)).Status);
    }

    private ZLinkRedisLocationStore CreateCrossLanguageStore(string language)
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        var prefix = Environment.GetEnvironmentVariable("ZLINK_REDIS_CROSS_LANGUAGE_PREFIX");
        Skip.If(string.IsNullOrWhiteSpace(prefix), "ZLINK_REDIS_CROSS_LANGUAGE_PREFIX is not set.");

        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions
        {
            ConnectionString = _fixture.ConnectionString,
            KeyPrefix = $"{prefix}:{language}"
        });
    }

    private static ZLinkPeerLocation DotnetPeer() => new(
        ZLinkLocationAutoConnectType.RouteMesh,
        "cross",
        RoutingId.From("dotnet-node"),
        ZLinkLocationRole.Router,
        "tcp://127.0.0.1:5310",
        100,
        7,
        new Dictionary<string, string> { ["route-endpoint"] = "tcp://127.0.0.1:6310" },
        ["dotnet", "route"],
        "dotnet-owner",
        0,
        default);

    private static ZLinkSpotLocation DotnetSpot() => new(
        "cross",
        RoutingId.From("dotnet-spot"),
        "dotnet-game",
        RoutingId.From("dotnet-node"),
        ZLinkSpotKind.User,
        "tcp://127.0.0.1:5310",
        "dotnet-owner",
        0,
        default);

    private static ZLinkActorLocation DotnetActor() => new(
        "player",
        "dotnet-actor",
        "dotnet-ref",
        RoutingId.From("dotnet-node"),
        0,
        ZLinkSpotKind.User,
        RoutingId.From("dotnet-spot"),
        ZLinkSpotKind.User,
        "dotnet-owner",
        default)
    {
        SpotMeshName = "cross"
    };

    private static ZLinkRouteLocation DotnetRoute() => new(
        ZLinkRouteKind.ActorSession,
        "dotnet-route",
        RoutingId.From("dotnet-node"),
        "dotnet-owner",
        0,
        new byte[] { 9, 8, 7, 6 },
        default);
}
