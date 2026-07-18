using PubSub.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;

namespace PubSub.Server.Subscriber;

public static class OperationalEndpoints
{
    public static WebApplication MapOperationalEndpoints(this WebApplication app, string role, string rid)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role, rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapGet("/locations/peers", async (
            string mesh,
            IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var rows = await query.ListMeshNodeDescriptorsAsync(mesh, cancellationToken);
            return Results.Ok(rows.Select(row =>
                new PeerLocationRowRes(row.Rid.ToString(), row.Endpoint)).ToArray());
        });
        app.MapPost("/locations/peers/wait", async (
            PeerLocationWaitReq request,
            IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var deadline = DateTimeOffset.UtcNow + timeout;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var matches = (await query.ListMeshNodeDescriptorsAsync(
                        request.MeshName,
                        cancellationToken))
                    .Where(row => row.Rid.Equals(RoutingId.From(request.NodeRid))
                                  && (request.Endpoint is null || row.Endpoint == request.Endpoint))
                    .ToArray();
                if (request.Present ? matches.Length == 1 : matches.Length == 0)
                    return Results.Ok(new { reached = true });
                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            }

            return Results.Problem(
                $"Peer row did not reach requested state for rid={request.NodeRid}.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/evidence/wait", async (
            EvidenceWaitReq request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var snapshot = await evidence.WaitUntilAsync(
                entries => request.ContainsAll.All(expected =>
                               entries.Skip(request.AfterIndex)
                                   .Any(entry => entry.Contains(expected, StringComparison.Ordinal)))
                           && request.ContainsAnyGroups.All(group =>
                               group.Any(expected =>
                                   entries.Skip(request.AfterIndex)
                                       .Any(entry => entry.Contains(expected, StringComparison.Ordinal))))
                           && request.ContainsAllLineGroups.All(group =>
                               entries.Skip(request.AfterIndex).Any(entry =>
                                   group.All(expected => entry.Contains(expected, StringComparison.Ordinal))))
                           && (request.ContainsAnyLineGroups.Length == 0
                               || request.ContainsAnyLineGroups.Any(group =>
                                   entries.Skip(request.AfterIndex).Any(entry =>
                                       group.All(expected => entry.Contains(expected, StringComparison.Ordinal))))),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot.Skip(request.AfterIndex).ToArray());
        });
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
        return app;
    }
}
