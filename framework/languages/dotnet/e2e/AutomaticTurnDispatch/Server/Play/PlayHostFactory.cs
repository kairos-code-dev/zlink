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
            framework.AddRouteMeshChannel(AutomaticTurnDispatchNames.ControlChannel)
                .EnableServer(options.ControlEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddRequestHandler<BindAwaitActorsControlHandler, BindAwaitActorsReq, BindAwaitActorsRes>(
                    "BindAwaitActorsReq")
                .AddRequestHandler<EnsureSpotControlHandler, EnsureSpotReq, EnsureSpotRes>("EnsureSpotReq")
                .AddRequestHandler<AwaitEvidenceControlHandler, AwaitEvidenceReq, AwaitEvidenceRes>(
                    "AwaitEvidenceReq")
                .AddRequestHandler<AwaitEvidenceWaitControlHandler, AwaitEvidenceWaitReq, AwaitEvidenceRes>(
                    "AwaitEvidenceWaitReq");
            framework.AddClientServerChannel(AutomaticTurnDispatchNames.DelayChannel)
                .EnableClient(options.DelayEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddRouteMeshChannel(AutomaticTurnDispatchNames.SpotRouteChannel)
                .EnableServer(options.SpotRouteEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddRouteMesh(AutomaticTurnDispatchNames.SpotChannel)
                                .Listen(options.SpotRouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<AwaitEntrySpot>()
                .AddActorFactory<AwaitActorFactory>(AutomaticTurnDispatchNames.ActorType)
                .AddSpotFactory<AwaitProbeSpot>();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "play", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}
