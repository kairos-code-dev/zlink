using Microsoft.Extensions.Configuration;

using Systems.Zlink;
using AutomaticTurnDispatch.Server.Play.Handlers;
using AutomaticTurnDispatch.Server.Play.Spots;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;

namespace AutomaticTurnDispatch.Server.Play;

internal static class PlayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = PlayOptions.Parse(args);
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
        builder.Services.AddZLinkHttpClient("external-api", http => http
            .BaseUrl(options.ExternalApiBaseUrl)
            .Timeout(TimeSpan.FromSeconds(5)));
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
            var controlMesh = framework.AddRouteMesh(AutomaticTurnDispatchNames.ControlChannel)
                .Listen(options.ControlEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            controlMesh.ChannelName(AutomaticTurnDispatchNames.ControlChannel);
            controlMesh
                .AddRouteRequestHandler<BindAwaitActorsControlHandler, BindAwaitActorsReq, BindAwaitActorsRes>(
                    "BindAwaitActorsReq")
                .AddRouteRequestHandler<EnsureSpotControlHandler, EnsureSpotReq, EnsureSpotRes>("EnsureSpotReq")
                .AddRouteRequestHandler<AwaitEvidenceControlHandler, AwaitEvidenceReq, AwaitEvidenceRes>(
                    "AwaitEvidenceReq")
                .AddRouteRequestHandler<AwaitEvidenceWaitControlHandler, AwaitEvidenceWaitReq, AwaitEvidenceRes>(
                    "AwaitEvidenceWaitReq");
            var delayMesh = framework.AddRouteMesh(AutomaticTurnDispatchNames.DelayChannel)
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From(options.Rid));
            delayMesh.ChannelName(AutomaticTurnDispatchNames.DelayChannel).SetWeight(0);
            delayMesh.PeerConnections.Connect(options.DelayEndpoint);
            var spotRouteMesh = framework.AddRouteMesh(AutomaticTurnDispatchNames.SpotRouteChannel)
                .Listen(options.SpotRouteEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            spotRouteMesh.ChannelName(AutomaticTurnDispatchNames.SpotRouteChannel);
            var mesh24 = framework.AddRouteMesh(AutomaticTurnDispatchNames.SpotChannel)
                                .Listen(options.SpotRouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<AwaitEntrySpot>()
                .AddActorFactory<AwaitActorFactory>(AutomaticTurnDispatchNames.ActorType)
                .AddSpotFactory<AwaitProbeSpot>();
            mesh24.ChannelName(AutomaticTurnDispatchNames.SpotChannel);
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "play", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}
