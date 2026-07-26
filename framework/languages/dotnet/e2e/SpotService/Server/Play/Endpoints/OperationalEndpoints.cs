using System.Diagnostics;
using SpotService.Shared;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Locations;

namespace SpotService.Server.Play.Endpoints;

internal static class OperationalEndpoints
{
    public static void MapOperationalEndpoints(WebApplication app, ServerOptions options)
    {
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
        app.MapPost("/channel/control-ping", async (
            NodeOptions node,
            EvidenceStore evidence,
            ControlPingReq request) =>
        {
            await Task.Yield();
            evidence.Add($"control-ping|rid={node.Rid}|value={request.Value}");
            return Results.Ok(new ControlPingRes(request.Value, node.Rid));
        });
        app.MapPost("/placement-weight", async (
            PlacementWeightReq request,
            NodeOptions node,
            IZLinkRouteMeshRuntimeOptions runtimeOptions,
            IZLinkLocationRuntimeQuery location,
            CancellationToken cancellationToken) =>
        {
            runtimeOptions.Mesh(SpotServiceNames.SpotChannel).PlacementWeight = request.Weight;
            var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
            while (DateTime.UtcNow < deadline)
            {
                var page = await location.ListMeshNodeDescriptorsAsync(
                    SpotServiceNames.SpotChannel,
                    cancellationToken: cancellationToken);
                if (page.Items.Any(descriptor =>
                        descriptor.Rid.ToString().StartsWith(
                            node.Rid + "-",
                            StringComparison.Ordinal)
                        && descriptor.PlacementWeight == request.Weight))
                {
                    return Results.Ok(new PlacementWeightRes(request.Weight));
                }
                await Task.Delay(TimeSpan.FromMilliseconds(20), cancellationToken);
            }
            return Results.StatusCode(StatusCodes.Status503ServiceUnavailable);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
    }
}
