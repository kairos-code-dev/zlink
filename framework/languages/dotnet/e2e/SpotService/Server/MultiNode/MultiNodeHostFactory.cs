using System.Diagnostics;
using SpotService.Server.MultiNode.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
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
                                  ?? throw new InvalidOperationException("--redis-key-prefix is required."))));
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
                var routeEndpoint = Require(options.MultiRouteAEndpoint, "--multi-route-a-endpoint");
                framework.AddRouteMesh(SpotServiceNames.MultiRouteChannelA)
                    .EnableServer(routeEndpoint)
                    .EnableClient(routeEndpoint)
                    .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeA))
                    .AddRequestHandler<MultiNodeCreateSpotAHandler, MultiNodeCreateSpotReq, MultiNodeCreateSpotRes>(
                        "MultiNodeCreateSpotReq");
                framework.AddSpotMesh(SpotServiceNames.MultiSpotNodeA)
                                        .EnableRouter(Require(options.MultiSpotRouterAEndpoint, "--multi-spot-router-a-endpoint"))
                    .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeA))
                    .AddSpotFactory<MultiNodeSpotA>();
            }

            if (isNodeB)
            {
                var routeEndpoint = Require(options.MultiRouteBEndpoint, "--multi-route-b-endpoint");
                framework.AddRouteMesh(SpotServiceNames.MultiRouteChannelB)
                    .EnableServer(routeEndpoint)
                    .EnableClient(routeEndpoint)
                    .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeB))
                    .AddRequestHandler<MultiNodeCreateSpotBHandler, MultiNodeCreateSpotReq, MultiNodeCreateSpotRes>(
                        "MultiNodeCreateSpotReq");
                framework.AddSpotMesh(SpotServiceNames.MultiSpotNodeB)
                                        .EnableRouter(Require(options.MultiSpotRouterBEndpoint, "--multi-spot-router-b-endpoint"))
                    .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeB))
                    .AddSpotFactory<MultiNodeSpotB>();
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
        app.MapPost("/spot/state/request", async (
            IZLinkRouteClient routes,
            IZLinkSpotLocationResolver locator,
            NodeOptions node,
            MultiNodeStateRouteReq request,
            CancellationToken cancellationToken) =>
        {
            var isNodeA = string.Equals(node.Rid, SpotServiceNames.MultiSpotNodeA, StringComparison.Ordinal);
            var channelName = isNodeA
                ? SpotServiceNames.MultiRouteChannelA
                : SpotServiceNames.MultiRouteChannelB;
            var result = await MultiNodeScenario.RequestStateWithRetryAsync(
                routes,
                locator,
                channelName,
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
        app.MapPost("/crash", () =>
        {
            ThreadPool.QueueUserWorkItem(_ =>
            {
                Thread.Sleep(50);
                Process.GetCurrentProcess().Kill(false);
            });
            return Results.Accepted();
        });
        return app;
    }

    private static string Require(string? value, string optionName)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{optionName} is required.")
            : value;
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