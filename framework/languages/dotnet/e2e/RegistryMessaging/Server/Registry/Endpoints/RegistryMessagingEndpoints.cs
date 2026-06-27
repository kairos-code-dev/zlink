using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.Contracts.Registry;
using RegistryMessaging.Server.Registry.Configuration;
using RegistryMessaging.Server.Registry.Infrastructure;

namespace RegistryMessaging.Server.Registry.Endpoints;

internal static class RegistryMessagingEndpoints
{
    public static void MapRegistryMessagingEndpoints(this WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/registry/topology", async (IZLinkRegistryQueryClient query) =>
        {
            var topology = await query.TopologyAsync(
                new ZLinkRegistryTopologyFilter(ChannelName: "profile"),
                CancellationToken.None);
            return Results.Ok(topology);
        });
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
        {
            evidence.Clear();
            return Results.Ok(new { status = "cleared" });
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
    }
}
