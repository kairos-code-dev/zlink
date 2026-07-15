using StackExchange.Redis;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Locations.Redis;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Application.Node;
using ZoneWorld.Server.ZoneNode.Application.Zone;
using ZoneWorld.Server.ZoneNode.Infrastructure.Store;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Actors;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Handlers;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Monitoring;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Spots;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Spots.Handlers;
using ZoneWorld.Server.ZoneNode.Ports;
using ZoneWorld.Shared.Contracts;

var configuration = ZoneWorldConfiguration.Load(args);
var shared = configuration.Shared;
var node = configuration.ZoneNode
           ?? throw new InvalidOperationException("ZoneNode configuration is required.");
var nodeId = node.NodeId;

// A node with no zones is the probe of §11.1: it hosts nothing, serves nothing, and is known
// to no other role. All it does is subscribe to the broadcast channel, which is what makes it
// evidence for ZW-D2 — Ops publishes to a node it was never told about.
var zones = ZoneTopology.ZonesOf(nodeId);
var hostsZones = zones.Count > 0;

var builder = Host.CreateApplicationBuilder(args);
builder.Logging.ClearProviders();
builder.Logging.AddSimpleConsole(console =>
{
    console.SingleLine = true;
    console.TimestampFormat = "HH:mm:ss.fff ";
});

builder.Services.AddSingleton(shared);
builder.Services.AddSingleton(node);
builder.Services.AddSingleton<IConnectionMultiplexer>(
    _ => ConnectionMultiplexer.Connect(shared.RedisEndpoint));
builder.Services.AddSingleton<IMaintenanceStorePort>(services =>
    new MaintenanceStoreRepository(
        services.GetRequiredService<IConnectionMultiplexer>(),
        shared.RedisKeyPrefix));
builder.Services.AddSingleton(new NodeMaintenancePolicy(nodeId));
builder.Services.AddSingleton<NodePlayerCensus>();
builder.Services.AddSingleton<MoveUseCase>();
builder.Services.AddSingleton<PlayerMovement>();
builder.Services.AddSingleton<IOpsReportPort, OpsReportAdapter>();
builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSpotEvent>, LocalSpotEventHandler>();
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
        .TraceLabel(nodeId);
    options.AddHandlersFromAssemblyOf(typeof(ZoneSpot));

    // The node that hosts no zone registers the broadcast subscriber and nothing else — no
    // mesh, no bridge, no channel of its own (§11.1). Anything more would make it a node Ops
    // could have been told about, and ZW-D2 would stop meaning anything.
    if (!hostsZones)
    {
        options.AddFanoutChannel(ZoneWorldNames.BroadcastChannel)
            .EnableSubscriber()
            .AddPublishHandler<BroadcastProbeSubscriber, WorldAnnounceEvent>(nameof(WorldAnnounceEvent));
        return;
    }

    // The zone spots and the player actors. A player entering a zone joins the spot named
    // after it, and when that spot is on another node the join is the transfer — which is
    // why the transfer adapter is not optional (§2.6).
    options.AddSpotMesh(ZoneWorldNames.ZoneMesh)
        .EnableRouter(node.SpotRouterEndpoint)
        .SetRoutingId(node.NodeRid)
        .SetEntrySpotRoutingId(node.NodeRid)
        .EnablePubSub(node.SpotPubSubEndpoint)
        .AddEntrySpot<ZoneEntrySpot>()
        .AddActorFactory<PlayerActorFactory>(ZoneWorldNames.PlayerActorType)
        .AddActorTransferAdapter<PlayerActor, PlayerActorTransferAdapter>(ZoneWorldNames.PlayerActorType)
        .AddSpotFactory<ZoneSpot>();

    // Registered only so the runtime can build the spot bridge that carries the announcement
    // from a channel handler into this node's zone spots (§8.2). It is never used to address a
    // node from the application (§1.1).
    options.AddRouteMeshChannel(ZoneWorldNames.BridgeMesh)
        .EnableServer(node.BridgeEndpoint)
        .EnableClient()
        .SetRoutingId(node.NodeRid);

    // The node's own channel. Ops calls the channel named after the node, so a call
    // reaches this node and no other (§8.4).
    options.AddClientServerChannel(ZoneWorldNames.OpsChannel(nodeId))
        .EnableServer(node.OpsChannelEndpoint)
        .AddHandlerGroup(HandlerGroups.ZoneNode);

    // Only the node hosting the spawn zone serves this: it is the authority for the
    // maintenance admission check on a new entry (§2.3).
    if (ZoneTopology.SpawnNode == nodeId)
        options.AddClientServerChannel(ZoneWorldNames.ActorsChannel)
            .EnableServer(node.ActorsChannelEndpoint)
            .AddHandlerGroup(HandlerGroups.ZoneNode);

    options.AddFanoutChannel(ZoneWorldNames.BroadcastChannel)
        .EnableSubscriber()
        .AddPublishHandler<WorldAnnounceSubscriber, WorldAnnounceEvent>(nameof(WorldAnnounceEvent))
        .AddPublishHandler<NodeMaintenanceChangedSubscriber, NodeMaintenanceChangedEvent>(
            nameof(NodeMaintenanceChangedEvent));

    // The report channel carries this node's identity: Ops reads the socket events on its
    // server side and needs to know *which node* connected or went away (§8.1).
    options.AddClientServerChannel(ZoneWorldNames.ReportChannel)
        .EnableClient()
        .SetRoutingId(node.NodeRid);
});

if (hostsZones)
{
    builder.Services.AddZLinkMonitoring(monitor =>
    {
        // Ops cannot subscribe to a remote node's spot events, so this node watches its own
        // and reports them explicitly (§8.1).
        monitor.AddSpotEvents(ZoneWorldNames.ZoneSpotSource, TimeSpan.FromMilliseconds(200));
    });

    // Registered after the framework so they start after it: the bootstrap creates spots and
    // actors, and the runtime has to be accepting operations before it can.
    builder.Services.AddHostedService<ZoneNodeBootstrap>();
    builder.Services.AddHostedService<NodeStatusReporter>();
}
var host = builder.Build();

if (!hostsZones)
{
    // The probe node has no spots or actors to bring up, but the runner still waits for the
    // line that says a node has finished starting. It must not appear before the subscriber is
    // running, or an announcement can be published into a node that is not yet listening.
    await host.StartAsync();
    host.Services
        .GetRequiredService<ILoggerFactory>()
        .CreateLogger("ZoneWorld.BroadcastProbe")
        .LogInformation("topology=ready node={NodeId} zones=", nodeId);
    await host.WaitForShutdownAsync();
    return;
}

await host.RunAsync();
