using ResilienceLifecycle.Server.Registry.Configuration;
using ResilienceLifecycle.Server.Registry.Infrastructure;
using ResilienceLifecycle.Shared;
using Zlink.Framework.Contracts.Registry;

namespace ResilienceLifecycle.Server.Registry.Endpoints;

internal static class RegistryEndpoints
{
    public static void MapRegistryEndpoints(this WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/topology", async (IZLinkRegistryQueryClient queryClient) =>
        {
            var topology = await queryClient.TopologyAsync(
                new ZLinkRegistryTopologyFilter(ChannelName: ResilienceLifecycleNames.Channel),
                CancellationToken.None);
            return Results.Ok(topology.Select(entry => new TopologyEntryRes(
                entry.RoutingId?.ToString(),
                entry.Endpoint,
                entry.State.ToString())).ToArray());
        });
        app.MapPost("/topology/wait", async (
            TopologyWaitReq request,
            IZLinkRegistryQueryClient queryClient,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var deadline = DateTimeOffset.UtcNow + timeout;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var topology = await queryClient.TopologyAsync(
                    new ZLinkRegistryTopologyFilter(ChannelName: ResilienceLifecycleNames.Channel),
                    cancellationToken);
                var snapshot = topology.Select(entry => new TopologyEntryRes(
                    entry.RoutingId?.ToString(),
                    entry.Endpoint,
                    entry.State.ToString())).ToArray();
                var count = snapshot.Count(entry =>
                    entry.RoutingId == request.RoutingId && entry.State == request.State);
                if (count == request.ExpectedCount) return Results.Ok(snapshot);

                await Task.Delay(TimeSpan.FromMilliseconds(250), cancellationToken);
            }

            return Results.Problem(
                $"Topology did not reach {request.ExpectedCount} entries for {request.RoutingId}:{request.State}.",
                statusCode: StatusCodes.Status504GatewayTimeout);
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