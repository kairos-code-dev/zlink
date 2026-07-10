using Systems.Zlink;
using YieldDispatch.Server.Session.Support;
using YieldDispatch.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using SessionServerOptions = YieldDispatch.Server.Session.Support.SessionOptions;
using YieldStreamSession = YieldDispatch.Server.Session.Support.YieldSession;

namespace YieldDispatch.Server.Session;

internal static class SessionHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = SessionServerOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new NodeOptions(options.Rid));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix)));
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.AddRouteMeshChannel(YieldDispatchNames.ControlChannel)
                .EnableServer(options.ControlEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddRouteMeshChannel(YieldDispatchNames.SpotRouteChannel)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddSpotMesh(YieldDispatchNames.SpotChannel)
                                .EnableRouter(options.SpotRouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<SessionYieldEntrySpot>()
                .AddActorFactory<SessionYieldActorFactory>(YieldDispatchNames.ActorType);
            framework.AddStreamNode(YieldDispatchNames.StreamNode)
                .Bind(options.StreamEndpoint)
                .RegisterSession<YieldStreamSession>();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "session", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}
