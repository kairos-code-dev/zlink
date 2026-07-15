using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Locations.Redis;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.Gateway.Infrastructure.ZLink.Sessions;
using ZoneWorld.Shared.Contracts;

var configuration = ZoneWorldConfiguration.Load(args);
var shared = configuration.Shared;
var gateway = configuration.Gateway
              ?? throw new InvalidOperationException("Gateway configuration is required.");

var builder = Host.CreateApplicationBuilder(args);
builder.Logging.ClearProviders();
builder.Logging.AddSimpleConsole(console =>
{
    console.SingleLine = true;
    console.TimestampFormat = "HH:mm:ss.fff ";
});

builder.Services.AddSingleton(shared);
builder.Services.AddSingleton(gateway);
builder.Services.AddSingleton<PlayerSessionBinder>();

builder.Services.AddZLinkFramework(options =>
{
    options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString(shared.RedisEndpoint)
        .SetKeyPrefix(shared.RedisKeyPrefix)));
    // A stopped node's location rows live until its owner lease expires. The default lease is
    // 15s, which is longer than an operator — or a scenario — waits to see a node leave (§8.1).
    var locations = options.ConfigureLocations();
    locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
    locations.OwnerLeaseTtl = TimeSpan.FromSeconds(3);

    options.ConfigureDispatch()
        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
        .TraceLabel("gateway");
    options.AddHandlersFromAssemblyOf(typeof(PlayerSession));

    // The browser's end of the world.
    options.AddStreamNode(ZoneWorldNames.GatewayStreamNode)
        .Bind(gateway.StreamEndpoint)
        .RegisterSession<PlayerSession>();

    // The Gateway joins the spot mesh but hosts nothing in it — no entry spot, no actor
    // factory. Membership is what lets it bind a session to an actor living on a zone
    // node and relay packets to it.
    options.AddSpotMesh(ZoneWorldNames.ZoneMesh)
        .EnableRouter(gateway.SpotRouterEndpoint)
        .SetRoutingId(gateway.RoutingId)
        .EnablePubSub(gateway.SpotPubSubEndpoint);

    options.AddClientServerChannel(ZoneWorldNames.ActorsChannel)
        .EnableClient();
});

await builder.Build().RunAsync();
