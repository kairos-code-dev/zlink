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
using Zlink.Framework.E2E.Diagnostics;

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
        var evidence = new EvidenceStore(options.Rid, options.EvidenceFile);
        builder.Services.AddSingleton(evidence);
        builder.Services.AddSingleton<SessionBindingProbeStore>();
        builder.Services.AddSingleton(new NodeOptions(options.Rid));
        builder.Services.AddSingleton(new E2eMessageFlowListener(
            Path.Combine(options.LogDir, $"{options.Rid}-flow.log"),
            options.Rid,
            flow =>
            {
                if (flow.Phase is not ("error" or "dropped")) return;
                evidence.Add(
                    "dispatch-error"
                    + $"|surface={flow.Surface}"
                    + $"|reason={flow.Reason}"
                    + $"|action={flow.Action}"
                    + $"|packet={flow.PacketName ?? "<null>"}");
            }));

        builder.Services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => { redis.ConnectionString = options.RedisEndpoint; redis.KeyPrefix = options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."); }));
                framework.AddRelocationStore(new ZLinkRedisRelocationStore(redis => { redis.ConnectionString = options.RedisEndpoint; redis.KeyPrefix = $"{options.RedisKeyPrefix}:relocation"; }));
                // Crash-recovery scenarios re-claim actors from a killed
                // node; a short owner lease keeps that takeover window
                // within the scenario's patience.
                var locations = framework.ConfigureLocations();
                locations.OwnerLeaseRenewInterval = TimeSpan.FromSeconds(1);
                locations.OwnerLeaseTtl = TimeSpan.FromSeconds(10);
                locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            }
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch().Diagnostics
                .SetLevel(ZLinkDiagnosticsLevel.Normal);
            var controlMesh = framework.AddRouteMesh(SpotServiceNames.ControlChannel)
                .Listen(Require(options.ControlEndpoint, "ControlEndpoint"))
                .SetRoutingIdPrefix(options.Rid);
            controlMesh.Channel(SpotServiceNames.ControlChannel).Server();
            controlMesh
                .AddRouteRequestHandler<EnsureActorHandler>()
                .AddRouteRequestHandler<ControlPingHandler>()
                .AddRouteRequestHandler<CreateSpotHandler>()
                .AddRouteRequestHandler<CloseSpotHandler>()
                .AddRouteRequestHandler<SpotTypeMismatchHandler>();
            var mesh22 = framework.AddRouteMesh(SpotServiceNames.SpotChannel)
                .Listen(Require(options.SpotRouterEndpoint, "SpotRouterEndpoint"))
                .SetRoutingIdPrefix(options.Rid)
                .SetPlacementWeight(0);
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
            mesh22.Channel(SpotServiceNames.SpotChannel).Server();
            framework.AddStreamNode(SpotServiceNames.StreamNode)
                .Bind(Require(options.StreamEndpoint, "StreamEndpoint"))
                .EnableActorDispatch()
                .AddSession<ScenarioSession>();
            if (!string.IsNullOrWhiteSpace(options.TlsStreamEndpoint))
                framework.AddStreamNode(SpotServiceNames.TlsStreamNode)
                    .Bind(options.TlsStreamEndpoint)
                    .EnableActorDispatch()
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
            var peer = ResolveReadyPeer(
                meshRuntime,
                SpotServiceNames.SpotChannel,
                targetRid);
            return peer is not null
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
            IZLinkRouteClient route,
            IZLinkRouteMeshRuntime meshRuntime) =>
        {
            var reply = await route.RequestToNode(
                    SpotServiceNames.ControlChannel,
                    ResolvePeerRoutingId(
                        meshRuntime,
                        SpotServiceNames.ControlChannel,
                        targetRid),
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

    internal static RoutingId ResolvePeerRoutingId(
        IZLinkRouteMeshRuntime meshRuntime,
        string meshName,
        string ridOrPrefix)
    {
        var peer = ResolveReadyPeer(meshRuntime, meshName, ridOrPrefix);
        return peer?.NodeRid
               ?? throw new InvalidOperationException(
                   $"Mesh '{meshName}' has no Ready node for prefix '{ridOrPrefix}'.");
    }

    private static ZLinkPeerStatus? ResolveReadyPeer(
        IZLinkRouteMeshRuntime meshRuntime,
        string meshName,
        string ridOrPrefix)
    {
        var readyPeers = meshRuntime.GetStatus(meshName).Peers.Where(candidate =>
        {
            var candidateRid = candidate.NodeRid.ToString();
            return candidate.State == ZLinkPeerState.Ready
                   && (string.Equals(candidateRid, ridOrPrefix, StringComparison.Ordinal)
                       || candidateRid.StartsWith(
                           $"{ridOrPrefix}-",
                           StringComparison.Ordinal));
        }).ToArray();

        return readyPeers.Length switch
        {
            0 => null,
            1 => readyPeers[0],
            _ => throw new InvalidOperationException(
                $"Mesh '{meshName}' has more than one Ready node for prefix "
                + $"'{ridOrPrefix}': "
                + string.Join(", ", readyPeers.Select(
                    static peer => peer.NodeRid.ToString()))
                + ".")
        };
    }

    private static string Require(string? value, string optionName)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{optionName} is required.")
            : value;
    }
}
