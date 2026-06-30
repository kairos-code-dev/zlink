using DiscoveryRegistryHa.Shared;
using Microsoft.AspNetCore.Mvc;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Registry;

namespace DiscoveryRegistryHa.Server.Registry;

internal static class RegistryHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, "registry");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddZLinkRegistry(registry =>
        {
            registry.RegistryId = options.RegistryId;
            registry.PubEndpoint = Require(options.RegistryPubEndpoint, "--registry-pub-endpoint");
            registry.RouterEndpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
            registry.HeartbeatInterval = TimeSpan.FromMilliseconds(250);
            registry.HeartbeatTimeout = TimeSpan.FromSeconds(2);
            registry.BroadcastInterval = TimeSpan.FromMilliseconds(250);
            foreach (var peer in options.PeerPubEndpoints) registry.AddPeer(peer);
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/registry/status",
            async ([FromServices] IZLinkRegistryQuery query, CancellationToken cancellationToken) =>
            Results.Ok(await query.StatusAsync(cancellationToken)));
        app.MapGet("/registry/topology",
            async ([FromServices] IZLinkRegistryQuery query, CancellationToken cancellationToken) =>
            Results.Ok(await query.TopologyAsync(
                new ZLinkRegistryTopologyFilter(ChannelName: DiscoveryRegistryHaNames.Channel),
                cancellationToken)));
        app.MapPost("/registry/topology/wait", async (
            TopologyReadyWaitRequest request,
            [FromServices] IZLinkRegistryQuery query,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var deadline = DateTimeOffset.UtcNow + timeout;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var topology = await query.TopologyAsync(
                    new ZLinkRegistryTopologyFilter(ChannelName: DiscoveryRegistryHaNames.Channel),
                    cancellationToken);
                if (topology.Count(entry =>
                        entry.State == ZLinkTopologyState.Ready
                        && entry.ServiceRole == ZLinkServiceRole.Router) >= request.ReadyCount)
                    return Results.Ok(topology);

                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            }

            throw new TimeoutException("Timed out waiting for discovery registry HA topology.");
        });
        app.MapGet("/registry/members",
            async ([FromServices] IZLinkRegistryQuery query, CancellationToken cancellationToken) =>
            Results.Ok(await query.MemberPeersAsync(DiscoveryRegistryHaNames.Channel, cancellationToken)));
        app.MapPost("/registry/members/wait", async (
            MemberEndpointWaitRequest request,
            [FromServices] IZLinkRegistryQuery query,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var deadline = DateTimeOffset.UtcNow + timeout;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var members = await query.MemberPeersAsync(DiscoveryRegistryHaNames.Channel, cancellationToken);
                if (members.Any(member =>
                        member.ServiceRole == ZLinkServiceRole.Router
                        && string.Equals(member.Endpoint, request.Endpoint, StringComparison.Ordinal)))
                    return Results.Ok(members);

                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            }

            throw new TimeoutException("Timed out waiting for discovery registry HA member endpoint.");
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }

    private static string Require(string? value, string name)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{name} is required.")
            : value;
    }
}