using System.Diagnostics;
using SpotService.Shared;

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
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
    }
}
