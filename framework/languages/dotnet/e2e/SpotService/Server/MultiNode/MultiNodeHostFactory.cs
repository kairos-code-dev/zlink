using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;

namespace SpotService.Server.MultiNode;

internal static partial class MultiNodeHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, defaultRole: "multi-node");
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
            var isNodeA = string.Equals(options.Rid, SpotServiceNames.MultiSpotNodeA, StringComparison.Ordinal);
            var isNodeB = string.Equals(options.Rid, SpotServiceNames.MultiSpotNodeB, StringComparison.Ordinal);
            if (!isNodeA && !isNodeB)
            {
                throw new InvalidOperationException(
                    $"multi-node role requires rid '{SpotServiceNames.MultiSpotNodeA}' or '{SpotServiceNames.MultiSpotNodeB}'.");
            }

            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
            if (isNodeA)
            {
                var routeEndpoint = Require(options.MultiRouteAEndpoint, "--multi-route-a-endpoint");
                framework.AddRouteMesh(SpotServiceNames.MultiRouteChannelA)
                    .EnableServer(routeEndpoint)
                    .EnableClient(routeEndpoint)
                    .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeA))
                    .AddRequestHandler<MultiNodeCreateSpotAHandler, MultiNodeCreateSpotReq, MultiNodeCreateSpotReply>("MultiNodeCreateSpotReq");
                framework.AddSpotMesh(SpotServiceNames.MultiSpotNodeA)
                    .UseRegistrySpotResolver()
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
                    .AddRequestHandler<MultiNodeCreateSpotBHandler, MultiNodeCreateSpotReq, MultiNodeCreateSpotReply>("MultiNodeCreateSpotReq");
                framework.AddSpotMesh(SpotServiceNames.MultiSpotNodeB)
                    .UseRegistrySpotResolver()
                    .EnableRouter(Require(options.MultiSpotRouterBEndpoint, "--multi-spot-router-b-endpoint"))
                    .SetRoutingId(RoutingId.From(SpotServiceNames.MultiSpotNodeB))
                    .AddSpotFactory<MultiNodeSpotB>();
            }
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/wait", async (
            EvidenceWaitRequest request,
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
                ? await CreateLocalMultiNodeSpotAsync<MultiNodeSpotA>(spots, evidence, node.Rid, request.SpotRid, cancellationToken)
                : await CreateLocalMultiNodeSpotAsync<MultiNodeSpotB>(spots, evidence, node.Rid, request.SpotRid, cancellationToken);
            return Results.Ok(created);
        });
        app.MapPost("/spot/state/request", async (
            IZLinkRouteClient routes,
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
                System.Diagnostics.Process.GetCurrentProcess().Kill(entireProcessTree: false);
            });
            return Results.Accepted();
        });
        return app;
    }

static string Require(string? value, string optionName)
    => string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{optionName} is required.")
        : value;

static async Task<MultiNodeCreateSpotReply> CreateLocalMultiNodeSpotAsync<TSpot>(
    IZLinkSpotManager spots,
    EvidenceStore evidence,
    string nodeRid,
    string spotRid,
    CancellationToken cancellationToken)
    where TSpot : class, IZLinkSpot
{
    var created = await spots.GetOrCreateAsync<TSpot>(RoutingId.From(spotRid), cancellationToken);
    evidence.Add($"multi-create-spot|node={nodeRid}|spot={created.SpotRid}|state={created.State}");
    return new MultiNodeCreateSpotReply(
        created.SpotRid.ToString(),
        nodeRid,
        created.State.ToString(),
        0);
}

}
