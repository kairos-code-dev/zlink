using Microsoft.Extensions.Configuration;

using System.Diagnostics;
using SpotService.Server.MultiNode.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Spots;

using Zlink.Framework.Locations.Redis;

using Zlink.Framework.Contracts.Locations;

namespace SpotService.Server.MultiNode;

internal static class MultiNodeHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, "multi-node");
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
                // Crash-recovery scenarios re-claim actors from a killed
                // node; a short owner lease keeps that takeover window
                // within the scenario's patience.
                var locations = framework.ConfigureLocations();
                locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
                locations.OwnerLeaseTtl = TimeSpan.FromSeconds(3);
                locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            }
            var isNodeA = string.Equals(options.Rid, SpotServiceNames.MultiSpotNodeA, StringComparison.Ordinal);
            var isNodeB = string.Equals(options.Rid, SpotServiceNames.MultiSpotNodeB, StringComparison.Ordinal);
            if (!isNodeA && !isNodeB)
                throw new InvalidOperationException(
                    $"multi-node role requires rid '{SpotServiceNames.MultiSpotNodeA}' or '{SpotServiceNames.MultiSpotNodeB}'.");

            framework.AddHandlersFromAssemblyOf(typeof(Program));
            if (isNodeA)
            {
                if (!string.IsNullOrWhiteSpace(options.MultiRouteAEndpoint))
                {
                    var routeEndpoint = options.MultiRouteAEndpoint;
                    var routeMesh = framework.AddRouteMesh(SpotServiceNames.MultiRouteChannelA)
                        .Listen(routeEndpoint)
                        .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeA));
                    routeMesh.ChannelName(SpotServiceNames.MultiRouteChannelA);
                    routeMesh.AddRouteRequestHandler<
                        MultiNodeCreateSpotAHandler,
                        MultiNodeCreateSpotReq,
                        MultiNodeCreateSpotRes>("MultiNodeCreateSpotReq");
                }

                var mesh20 = framework.AddRouteMesh(ResolveSpotMeshName(options))
                    .Listen(Require(options.MultiSpotRouterAEndpoint, "MultiSpotRouterAEndpoint"))
                    .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeA))
                    .AddEntrySpot<ScenarioEntrySpot>()
                    .AddActorFactory<ScenarioActorFactory>(SpotServiceNames.ActorType)
                    .AddSpotFactory<SpotOnlyUserSpot>()
                    .AddSpotFactory<ScenarioUserSpot>()
                    .AddSpotFactory<MultiNodeSpotA>();
                mesh20.ChannelName(ResolveSpotMeshName(options));
            }

            if (isNodeB)
            {
                if (!string.IsNullOrWhiteSpace(options.MultiRouteBEndpoint))
                {
                    var routeEndpoint = options.MultiRouteBEndpoint;
                    var routeMesh = framework.AddRouteMesh(SpotServiceNames.MultiRouteChannelB)
                        .Listen(routeEndpoint)
                        .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeB));
                    routeMesh.ChannelName(SpotServiceNames.MultiRouteChannelB);
                    routeMesh.AddRouteRequestHandler<
                        MultiNodeCreateSpotBHandler,
                        MultiNodeCreateSpotReq,
                        MultiNodeCreateSpotRes>("MultiNodeCreateSpotReq");
                }

                var mesh21 = framework.AddRouteMesh(ResolveSpotMeshName(options))
                    .Listen(Require(options.MultiSpotRouterBEndpoint, "MultiSpotRouterBEndpoint"))
                    .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeB))
                    .AddEntrySpot<ScenarioEntrySpot>()
                    .AddActorFactory<ScenarioActorFactory>(SpotServiceNames.ActorType)
                    .AddSpotFactory<SpotOnlyUserSpot>()
                    .AddSpotFactory<ScenarioUserSpot>()
                    .AddSpotFactory<MultiNodeSpotB>();
                mesh21.ChannelName(ResolveSpotMeshName(options));
            }
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
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
        app.MapPost("/spot/create-local", async (
            IZLinkSpotManager spots,
            EvidenceStore evidence,
            NodeOptions node,
            MultiNodeCreateSpotReq request,
            CancellationToken cancellationToken) =>
        {
            var isNodeA = string.Equals(node.Rid, SpotServiceNames.MultiSpotNodeA, StringComparison.Ordinal);
            var created = isNodeA
                ? await CreateLocalMultiNodeSpotAsync<MultiNodeSpotA>(spots, evidence, node.Rid, request.SpotRid,
                    cancellationToken)
                : await CreateLocalMultiNodeSpotAsync<MultiNodeSpotB>(spots, evidence, node.Rid, request.SpotRid,
                    cancellationToken);
            return Results.Ok(created);
        });
        app.MapPost("/spot/create-user-local", async (
            IZLinkSpotManager spots,
            EvidenceStore evidence,
            NodeOptions node,
            CreateSpotReq request,
            CancellationToken cancellationToken) =>
        {
            var created = await spots.GetOrCreateAsync<SpotOnlyUserSpot>(
                RoutingId.From(request.SpotRid),
                cancellationToken);
            evidence.Add($"create-user-spot|rid={node.Rid}|spot={created.SpotRid}|state={created.State}");
            return Results.Ok(new CreateSpotRes(
                created.SpotRid.ToString(),
                node.Rid,
                created.State.ToString()));
        });
        app.MapPost("/spot/spot-only/request-send", async (
            IZLinkSpotManager spots,
            EvidenceStore evidence,
            NodeOptions node,
            SpotOnlyMeshReq request,
            CancellationToken cancellationToken) =>
        {
            var created = await spots.GetOrCreateAsync<SpotOnlyUserSpot, SpotOnlyMeshReq>(
                RoutingId.From(request.SourceSpotRid),
                request,
                cancellationToken);
            var snapshot = await evidence.WaitUntilAsync(
                entries => entries.Any(entry =>
                    entry.Contains(
                        $"spot-only-request|rid={node.Rid}|source={request.SourceSpotRid}|target={request.TargetSpotRid}",
                        StringComparison.Ordinal)
                    && entry.Contains($"|marker={request.Marker}", StringComparison.Ordinal)),
                TimeSpan.FromSeconds(10),
                cancellationToken);
            return Results.Ok(new SpotOnlyMeshRes(
                created.SpotRid.ToString(),
                request.TargetSpotRid,
                ExtractSpotOnlyValue(snapshot, request),
                request.Marker));
        });
        app.MapPost("/actor/spot-only-join", async (
            IZLinkActorManager actors,
            IZLinkActorClient actorClient,
            EvidenceStore evidence,
            NodeOptions node,
            SpotOnlyJoinReq request,
            CancellationToken cancellationToken) =>
        {
            var actor = await actors
                .GetOrCreate(request.ActorId, SpotServiceNames.ActorType)
                .Request(new ScenarioActorCreateReq($"spot-only-{request.ActorId}"))
                .Async(cancellationToken) switch
            {
                ZLinkActorCreateResult.Existing value => value.Actor,
                ZLinkActorCreateResult.Created value => value.Actor,
                _ => throw new InvalidOperationException("Actor creation was rejected.")
            };
            var result = await actorClient.RequestToActor(
                    ResolveSpotMeshName(options),
                    actor,
                    request)
                .Timeout(TimeSpan.FromSeconds(10))
                .Async<SpotOnlyJoinRes>(cancellationToken);
            await evidence.WaitUntilAsync(
                entries => entries.Any(entry =>
                    entry.Contains(
                        $"spot-only-actor-join|rid={node.Rid}|actor={request.ActorId}|target={request.TargetSpotRid}",
                        StringComparison.Ordinal)
                    && entry.Contains($"|marker={request.Marker}", StringComparison.Ordinal)),
                TimeSpan.FromSeconds(10),
                cancellationToken);
            return Results.Ok(result);
        });
        app.MapPost("/spot/state/request", async (
            IZLinkSpotClient spotsClient,
            IZLinkSpotHandleResolver locator,
            NodeOptions node,
            MultiNodeStateRouteReq request,
            CancellationToken cancellationToken) =>
        {
            var isNodeA = string.Equals(node.Rid, SpotServiceNames.MultiSpotNodeA, StringComparison.Ordinal);
            var result = await MultiNodeScenario.RequestStateAsync(
                spotsClient,
                locator,
                node.Rid,
                request.SpotRid,
                request.Delta,
                cancellationToken);
            return Results.Ok(result);
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

    private static string ResolveSpotMeshName(ServerOptions options)
    {
        return string.IsNullOrWhiteSpace(options.MultiRouteAEndpoint)
               && string.IsNullOrWhiteSpace(options.MultiRouteBEndpoint)
            ? SpotServiceNames.SpotOnlyMesh
            : options.Rid;
    }

    private static int ExtractSpotOnlyValue(string[] evidence, SpotOnlyMeshReq request)
    {
        const string key = "|value=";
        var entry = evidence.Last(line =>
            line.Contains($"source={request.SourceSpotRid}", StringComparison.Ordinal)
            && line.Contains($"target={request.TargetSpotRid}", StringComparison.Ordinal)
            && line.Contains($"marker={request.Marker}", StringComparison.Ordinal));
        var index = entry.IndexOf(key, StringComparison.Ordinal);
        if (index < 0) return 0;
        index += key.Length;
        var end = entry.IndexOf('|', index);
        var valueText = end < 0 ? entry[index..] : entry[index..end];
        return int.TryParse(valueText, out var value) ? value : 0;
    }

    private static async Task<MultiNodeCreateSpotRes> CreateLocalMultiNodeSpotAsync<TSpot>(
        IZLinkSpotManager spots,
        EvidenceStore evidence,
        string nodeRid,
        string spotRid,
        CancellationToken cancellationToken)
        where TSpot : class, IZLinkSpot
    {
        var created = await spots.GetOrCreateAsync<TSpot>(RoutingId.From(spotRid), cancellationToken);
        evidence.Add($"multi-create-spot|node={nodeRid}|spot={created.SpotRid}|state={created.State}");
        return new MultiNodeCreateSpotRes(
            created.SpotRid.ToString(),
            nodeRid,
            created.State.ToString(),
            0);
    }
}
