using StackExchange.Redis;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Locations.Redis;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.Ops.Application.Ops;
using ZoneWorld.Server.Ops.Infrastructure.Store;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Monitoring;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Sessions;
using ZoneWorld.Server.Ops.Ports;
using ZoneWorld.Shared.Contracts;

var shared = ZoneWorldSettings.Create();
var ops = OpsSettings.Create();

var builder = Host.CreateApplicationBuilder(args);
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
builder.Services.AddSingleton<AnnouncementService>();
builder.Services.AddSingleton<MaintenanceService>();
builder.Services.AddHostedService<NodeStatusBroadcaster>();
builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>, LocationEventHandler>();
builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, SocketEventHandler>();

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
        .TraceLabel("ops");
    options.AddHandlersFromAssemblyOf(typeof(OpsConsoleSession));

    options.AddStreamNode(ZoneWorldNames.OpsStreamNode)
        .Bind(ops.StreamEndpoint)
        .RegisterSession<OpsConsoleSession>();

    // The announcement and the maintenance change both leave here without a node list.
    // Adding a node changes nothing on this side — that is the whole point (ZW-D2).
    options.AddFanoutChannel(ZoneWorldNames.BroadcastChannel)
        .EnablePublisher(ops.BroadcastEndpoint);

    options.AddClientServerChannel(ZoneWorldNames.ReportChannel)
        .EnableServer(ops.ReportEndpoint)
        .AddHandlerGroup(HandlerGroups.Ops);

    // A node is addressed by the channel named after it, so the call lands on that node
    // and no other (§8.4).
    foreach (var nodeId in ZoneTopology.AllNodes)
        options.AddClientServerChannel(ZoneWorldNames.OpsChannel(nodeId))
            .EnableClient();
});

builder.Services.AddZLinkMonitoring(monitor =>
{
    // Node registration and connection are changes, not answers to a question: a node that
    // shut down cannot reply (§8.1).
    monitor.AddLocationRuntimeEvents(ZoneWorldNames.OpsLocationSource, TimeSpan.FromMilliseconds(300));
    monitor.AddSocketEvents(ZoneWorldNames.OpsSocketSource);
});

await builder.Build().RunAsync();
