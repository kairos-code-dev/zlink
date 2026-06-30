using DiscoveryRegistryHa.Shared;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Registry;

namespace DiscoveryRegistryHa.Server.Probe;

internal static class ProbeHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ProbeOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddZLinkRegistryQueryClient(query => query.Endpoint = options.RegistryRouterEndpoint);

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.RegistryRouterEndpoint }));
        app.MapGet("/registry/topology", async (
            [FromServices] IZLinkRegistryQueryClient query,
            CancellationToken cancellationToken) =>
            Results.Ok(await query.TopologyAsync(
                new ZLinkRegistryTopologyFilter(ChannelName: DiscoveryRegistryHaNames.Channel),
                cancellationToken)));
        app.MapPost("/registry/topology/wait", async (
            TopologyReadyWaitReq request,
            [FromServices] IZLinkRegistryQueryClient query,
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
                {
                    return Results.Ok(topology);
                }

                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            }

            throw new TimeoutException("Timed out waiting for discovery registry HA remote topology.");
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }
}
