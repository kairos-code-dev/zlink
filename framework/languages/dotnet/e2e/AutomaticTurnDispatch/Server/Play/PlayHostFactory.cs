using Microsoft.Extensions.Configuration;

using Systems.Zlink;
using AutomaticTurnDispatch.Server.Play.Handlers;
using AutomaticTurnDispatch.Server.Play.Spots;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;

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
            framework.AddRelocationStore(new ZLinkRedisRelocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix($"{options.RedisKeyPrefix}:relocation")));
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            var controlMesh = framework.AddRouteMesh(AutomaticTurnDispatchNames.ControlChannel)
                .Listen(options.ControlEndpoint)
                .SetRoutingIdPrefix(options.Rid);
            controlMesh.Channel(AutomaticTurnDispatchNames.ControlChannel).Server();
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
            delayMesh.Channel(AutomaticTurnDispatchNames.DelayChannel).Client();
            delayMesh.PeerConnections.Connect(options.DelayEndpoint);
            var spotRouteMesh = framework.AddRouteMesh(AutomaticTurnDispatchNames.SpotRouteChannel)
                .Listen(options.SpotRouteEndpoint)
                .SetRoutingIdPrefix(options.Rid);
            spotRouteMesh.Channel(AutomaticTurnDispatchNames.SpotRouteChannel).Server();
            var mesh24 = framework.AddRouteMesh(AutomaticTurnDispatchNames.SpotChannel)
                .Listen(options.SpotRouterEndpoint)
                .SetRoutingIdPrefix(options.Rid);
            mesh24.Objects().Server()
                .AddEntrySpot<AwaitEntrySpot>()
                .AddActorFactory<AwaitActor, AwaitActorFactory>(
                    AutomaticTurnDispatchNames.ActorType,
                    options: null,
                    ZLinkRelocationPolicy<AwaitActor>.Recreate)
                .AddSpotFactory(
                    AutomaticTurnDispatchNames.SpotType,
                    options: null,
                    ZLinkRelocationPolicy<AwaitProbeSpot>.Disabled);
            mesh24.Channel(AutomaticTurnDispatchNames.SpotChannel).Server();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "play", options.Rid }));
        app.MapGet("/topology/ready", async (
            string meshName,
            string rid,
            IZLinkRouteMeshRuntime runtime,
            IZLinkRouteClient routes,
            CancellationToken cancellationToken) =>
        {
            if (string.Equals(
                    meshName,
                    AutomaticTurnDispatchNames.DelayChannel,
                    StringComparison.Ordinal))
            {
                try
                {
                    _ = await routes.RequestToChannel(
                            meshName,
                            new DelayReq($"readiness-{rid}", 0, "readiness"))
                        .Timeout(TimeSpan.FromMilliseconds(500))
                        .Async<DelayRes>(cancellationToken);
                    return Results.Ok(new { ready = true });
                }
                catch (Exception error) when (
                    error is ZLinkFrameworkException
                        or TimeoutException
                        or OperationCanceledException)
                {
                    return Results.Ok(new { ready = false });
                }
            }

            var snapshot = runtime.Snapshot(meshName);
            var ready = snapshot.Peers.Any(peer =>
                peer.Ready
                && (string.Equals(
                        peer.Rid.ToString(),
                        rid,
                        StringComparison.Ordinal)
                    || peer.Rid.ToString().StartsWith(
                        $"{rid}-",
                        StringComparison.Ordinal)));
            return Results.Ok(new { ready });
        });
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}
