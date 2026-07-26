using Microsoft.Extensions.Configuration;
using StackExchange.Redis;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Configuration;
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
builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkMeshRuntimeEvent>, SocketEventHandler>();

builder.Services.AddZLinkFramework(options =>
{
    options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString(shared.RedisEndpoint)
        .SetKeyPrefix(shared.RedisKeyPrefix)));
    // These values govern registrations owned by Ops. Zone nodes keep the documented 30-second
    // defaults, so crash scenarios still exercise real lease expiry (§4.2 and §8.1).
    var locations = options.ConfigureLocations();
    locations.OwnerLeaseRenewInterval = TimeSpan.FromSeconds(1);
    locations.OwnerLeaseTtl = TimeSpan.FromSeconds(3);
    locations.OwnerLeaseFencingMargin = TimeSpan.FromSeconds(1);
    locations.OwnerLeaseRenewTimeout = TimeSpan.FromMilliseconds(500);

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

    var mesh = options.AddRouteMesh(ZoneWorldNames.MeshName)
        .Listen(ops.MeshEndpoint)
        .SetRoutingIdPrefix("ops");
    mesh.Channel(ZoneWorldNames.ReportChannel).Server()
        .AddHandlerGroup(HandlerGroups.Ops);
    mesh.Channel(ZoneWorldNames.ZoneChannel).Client();
});

builder.Services.AddZLinkMonitoring(monitor =>
{
    // Node registration and connection are changes, not answers to a question: a node that
    // shut down cannot reply (§8.1).
    monitor.AddLocationRuntimeEvents(ZoneWorldNames.OpsLocationSource, TimeSpan.FromMilliseconds(300));
    monitor.AddMeshNodeEvents(ZoneWorldNames.MeshName);
});

await builder.Build().RunAsync();
