using Microsoft.Extensions.Configuration;

using Systems.Zlink;
using AutomaticTurnDispatch.Server.Session.Support;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using SessionServerOptions = AutomaticTurnDispatch.Server.Session.Support.SessionOptions;
using AwaitStreamSession = AutomaticTurnDispatch.Server.Session.Support.AwaitSession;

namespace AutomaticTurnDispatch.Server.Session;

internal static class SessionHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = SessionServerOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
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
            framework.AddRouteMeshChannel(AutomaticTurnDispatchNames.ControlChannel)
                .EnableServer(options.ControlEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddRouteMeshChannel(AutomaticTurnDispatchNames.SpotRouteChannel)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddSpotMesh(AutomaticTurnDispatchNames.SpotChannel)
                                .EnableRouter(options.SpotRouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<SessionAwaitEntrySpot>()
                .AddActorFactory<SessionAwaitActorFactory>(AutomaticTurnDispatchNames.ActorType);
            framework.AddStreamNode(AutomaticTurnDispatchNames.StreamNode)
                .Bind(options.StreamEndpoint)
                .RegisterSession<AwaitStreamSession>();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "session", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}
