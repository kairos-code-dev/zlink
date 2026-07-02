using Systems.Zlink;
using YieldDispatch.Server.Play.Handlers;
using YieldDispatch.Server.Play.Spots;
using YieldDispatch.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace YieldDispatch.Server.Play;

internal static class PlayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = PlayOptions.Parse(args);
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
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.AddRouteMesh(YieldDispatchNames.ControlChannel)
                .EnableServer(options.ControlEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddRequestHandler<BindYieldActorsControlHandler, BindYieldActorsReq, BindYieldActorsRes>(
                    "BindYieldActorsReq")
                .AddRequestHandler<EnsureSpotControlHandler, EnsureSpotReq, EnsureSpotRes>("EnsureSpotReq")
                .AddRequestHandler<YieldEvidenceControlHandler, YieldEvidenceReq, YieldEvidenceRes>(
                    "YieldEvidenceReq")
                .AddRequestHandler<YieldEvidenceWaitControlHandler, YieldEvidenceWaitReq, YieldEvidenceRes>(
                    "YieldEvidenceWaitReq");
            framework.AddClientServerChannel(YieldDispatchNames.DelayChannel)
                .EnableClient(options.DelayEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddRouteMesh(YieldDispatchNames.SpotRouteChannel)
                .EnableServer(options.SpotRouteEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddSpotMesh(YieldDispatchNames.SpotChannel)
                                .EnableRouter(options.SpotRouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .EnablePubSub(options.SpotPubEndpoint)
                .AddEntrySpot<YieldEntrySpot>()
                .AddActorFactory<YieldActorFactory>(YieldDispatchNames.ActorType)
                .AddSpotFactory<YieldProbeSpot>();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "play", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}