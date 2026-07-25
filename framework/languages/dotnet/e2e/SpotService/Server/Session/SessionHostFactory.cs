using Microsoft.Extensions.Configuration;

using System.Diagnostics;
using SpotService.Server.Session.Handlers;
using SpotService.Server.Session.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;

using Zlink.Framework.Locations.Redis;

namespace SpotService.Server.Session;

internal static class SessionHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, "session");
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
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix(options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."))));
                framework.AddRelocationStore(new ZLinkRedisRelocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix($"{options.RedisKeyPrefix}:relocation")));
                // Crash-recovery scenarios re-claim actors from a killed
                // node; a short owner lease keeps that takeover window
                // within the scenario's patience.
                var locations = framework.ConfigureLocations();
                locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
                locations.OwnerLeaseTtl = TimeSpan.FromSeconds(10);
                locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            }
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            var controlMesh = framework.AddRouteMesh(SpotServiceNames.ControlChannel)
                .Listen(Require(options.ControlEndpoint, "ControlEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            controlMesh.ChannelName(SpotServiceNames.ControlChannel);
            if (!string.IsNullOrWhiteSpace(options.ControlPeerAEndpoint))
                controlMesh.PeerConnections.Connect(
                    RoutingId.From("play-a"),
                    options.ControlPeerAEndpoint);
            if (!string.IsNullOrWhiteSpace(options.ControlPeerBEndpoint))
                controlMesh.PeerConnections.Connect(
                    RoutingId.From("play-b"),
                    options.ControlPeerBEndpoint);
            controlMesh
                .AddRouteRequestHandler<EnsureActorHandler>()
                .AddRouteRequestHandler<ControlPingHandler>()
                .AddRouteRequestHandler<CreateSpotHandler>()
                .AddRouteRequestHandler<CloseSpotHandler>()
                .AddRouteRequestHandler<SpotTypeMismatchHandler>();
            var mesh22 = framework.AddRouteMesh(SpotServiceNames.SpotChannel)
                .Listen(Require(options.SpotRouterEndpoint, "SpotRouterEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            if (!string.IsNullOrWhiteSpace(options.SpotPeerAEndpoint))
                mesh22.PeerConnections.Connect(
                    RoutingId.From("play-a"),
                    options.SpotPeerAEndpoint);
            if (!string.IsNullOrWhiteSpace(options.SpotPeerBEndpoint))
                mesh22.PeerConnections.Connect(
                    RoutingId.From("play-b"),
                    options.SpotPeerBEndpoint);
            mesh22.Objects().Server()
                .AddEntrySpot<ScenarioEntrySpot>()
                .AddActorFactory<ScenarioActor, ScenarioActorFactory>(
                    SpotServiceNames.ActorType,
                    null,
                    ZLinkRelocationPolicy<ScenarioActor>.Recreate)
                .AddSpotFactory<ScenarioUserSpot>(
                    SpotServiceNames.UserSpotType,
                    null,
                    ZLinkRelocationPolicy<ScenarioUserSpot>.Disabled)
                .AddSpotFactory<ScenarioAlternateSpot>(
                    SpotServiceNames.AlternateSpotType,
                    null,
                    ZLinkRelocationPolicy<ScenarioAlternateSpot>.Disabled)
                .AddSpotFactory<MultiNodeSpotA>(
                    SpotServiceNames.MultiSpotTypeA,
                    null,
                    ZLinkRelocationPolicy<MultiNodeSpotA>.Disabled)
                .AddSpotFactory<MultiNodeSpotB>(
                    SpotServiceNames.MultiSpotTypeB,
                    null,
                    ZLinkRelocationPolicy<MultiNodeSpotB>.Disabled);
            mesh22.ChannelName(SpotServiceNames.SpotChannel);
            framework.AddStreamNode(SpotServiceNames.StreamNode)
                .Bind(Require(options.StreamEndpoint, "StreamEndpoint"))
                .EnableActorDispatch(SpotServiceNames.SpotChannel)
                .AddSession<ScenarioSession>();
            if (!string.IsNullOrWhiteSpace(options.TlsStreamEndpoint))
                framework.AddStreamNode(SpotServiceNames.TlsStreamNode)
                    .Bind(options.TlsStreamEndpoint)
                    .EnableActorDispatch(SpotServiceNames.SpotChannel)
                    .SetTlsServer(
                        Require(options.TlsCertPath, "TlsCertPath"),
                        Require(options.TlsKeyPath, "TlsKeyPath"))
                    .AddSession<ScenarioSession>();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/channel/spot-peer-ready/{targetRid}", (
            string targetRid,
            IZLinkRouteMeshRuntime meshRuntime) =>
        {
            var peer = meshRuntime.Snapshot(SpotServiceNames.SpotChannel).Peers
                .FirstOrDefault(candidate => string.Equals(
                    candidate.Rid.ToString(),
                    targetRid,
                    StringComparison.Ordinal));
            return peer is { Ready: true }
                ? Results.Ok(peer)
                : Results.StatusCode(StatusCodes.Status503ServiceUnavailable);
        });
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/wait", async (
            EvidenceWaitReq request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var snapshot = await evidence.WaitUntilAsync(
                entries => request.ContainsAll.All(expected =>
                    entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal))),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/channel/control-ping/{targetRid}", async (
            string targetRid,
            ControlPingReq request,
            IZLinkRouteClient route) =>
        {
            var reply = await route.RequestToNode(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(targetRid),
                    request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ControlPingRes>();
            return Results.Ok(reply);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }

    private static string Require(string? value, string optionName)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{optionName} is required.")
            : value;
    }
}
