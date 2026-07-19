using Microsoft.Extensions.Configuration;
using StackExchange.Redis;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Locations.Redis;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.Ops.Application.Ops;
using ZoneWorld.Server.Ops.Infrastructure.Store;
using ZoneWorld.Server.Ops.Infrastructure.ZLink;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Handlers;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Monitoring;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Sessions;
using ZoneWorld.Server.Ops.Ports;
using ZoneWorld.Shared.Contracts;

var configuration = ZoneWorldConfiguration.Load(args);
var shared = configuration.Shared;
var ops = configuration.Ops
          ?? throw new InvalidOperationException("Ops configuration is required.");

var builder = Host.CreateApplicationBuilder(args);
builder.Configuration.Sources.Clear();
builder.Configuration.AddInMemoryCollection();
builder.Logging.ClearProviders();
builder.Logging.AddSimpleConsole(console =>
{
    console.SingleLine = true;
    console.TimestampFormat = "HH:mm:ss.fff ";
});

builder.Services.AddSingleton(shared);
builder.Services.AddSingleton(ops);
builder.Services.AddSingleton<IConnectionMultiplexer>(
    _ => ConnectionMultiplexer.Connect(shared.RedisEndpoint));
builder.Services.AddSingleton<IMaintenanceStorePort>(services =>
    new MaintenanceStoreRepository(
        services.GetRequiredService<IConnectionMultiplexer>(),
        shared.RedisKeyPrefix));
builder.Services.AddSingleton<NodeRegistry>();
builder.Services.AddSingleton<OpsConsoleRegistry>();
builder.Services.AddSingleton<IWorldOperationsPort, WorldOperationsAdapter>();
builder.Services.AddSingleton<AnnouncementService>();
builder.Services.AddSingleton<MaintenanceService>();
builder.Services.AddSingleton<NodeDiagnosticsService>();
builder.Services.AddHostedService<NodeStatusBroadcaster>();
builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>, LocationEventHandler>();
builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, SocketEventHandler>();

builder.Services.AddZLinkFramework(options =>
{
    options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString(shared.RedisEndpoint)
        .SetKeyPrefix(shared.RedisKeyPrefix)));
    // These values govern registrations owned by Ops. Zone nodes keep the documented 30-second
    // defaults, so crash scenarios still exercise real lease expiry (§4.2 and §8.1).
    var locations = options.ConfigureLocations();
    locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
    locations.OwnerLeaseTtl = TimeSpan.FromSeconds(3);
    // Ops observes the zone mesh without joining it: the operational query
    // enumerates it only when declared here (§8.1).
    locations.ObservedMeshNames.Add(ZoneWorldNames.ZoneMesh);

    options.ConfigureDispatch()
        .MessageFlow(ZLinkMessageFlowLogMode.ErrorsOnly)
        .TraceLabel("ops");
    options.AddHandlersFromAssemblyOf(typeof(OpsConsoleSession));

    options.AddStreamNode(ZoneWorldNames.OpsStreamNode)
        .Bind(ops.StreamEndpoint)
        .AddSession<OpsConsoleSession>();

    // The announcement and the maintenance change both leave here without a node list.
    // Adding a node changes nothing on this side — that is the whole point (ZW-D2).
    options.AddFanoutChannel(ZoneWorldNames.BroadcastChannel)
        .EnablePublisher(ops.BroadcastEndpoint);

    var reportMesh = options.AddRouteMesh(ZoneWorldNames.ReportChannel)
        .Listen(ops.ReportEndpoint)
        // Discovery clients dial this server through its descriptor row,
        // which needs a concrete routing id to be advertised.
        .SetRoutingId(Systems.Zlink.RoutingId.From("zoneworld-ops-report"));
    reportMesh.ChannelName(ZoneWorldNames.ReportChannel)
        .AddSendHandler<ReportSpotEventHandler>()
        .AddSendHandler<ReportNodeStatusHandler>();

    // A node is addressed by the channel named after it, so the call lands on that node and no
    // other (§8.4). What is enumerated here is the §2 zone placement — the nodes an operator can
    // select — not a list of who is out there. The announcement above leaves without consulting
    // it, and a node outside the placement (§11.1) is unknown here yet still receives it (ZW-D2).
    foreach (var nodeId in ZoneTopology.ZoneNodes)
    {
        var channelName = ZoneWorldNames.OpsChannel(nodeId);
        var nodeMesh = options.AddRouteMesh(channelName)
            .Listen("tcp://127.0.0.1:0")
            .SetRoutingId(Systems.Zlink.RoutingId.From($"ops-{nodeId}"));
        nodeMesh.ChannelName(channelName).SetWeight(0);
    }
});

builder.Services.AddZLinkMonitoring(monitor =>
{
    // Node registration and connection are changes, not answers to a question: a node that
    // shut down cannot reply (§8.1).
    monitor.AddLocationRuntimeEvents(ZoneWorldNames.OpsLocationSource, TimeSpan.FromMilliseconds(300));
    monitor.AddSocketEvents(ZoneWorldNames.OpsSocketSource);
    foreach (var nodeId in ZoneTopology.ZoneNodes)
        monitor.AddSocketEvents(ZoneWorldNames.OpsChannelSocketSource(nodeId));
});

await builder.Build().RunAsync();
