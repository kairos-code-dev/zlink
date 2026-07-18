using LocationMessaging.Server.Provider.Configuration;
using LocationMessaging.Server.Provider.Infrastructure;
using LocationMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;

namespace LocationMessaging.Server.Provider.Endpoints;

internal static class ProviderEndpoints
{
    public static void MapProviderEndpoints(this WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        // Raw peer row check: cache-less runtime query straight to the store
        // (doc §1 verification basis).
        app.MapGet("/locations/peers", async (
            string? mesh,
            IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var peers = await query.ListMeshNodeDescriptorsAsync(
                mesh ?? throw new InvalidOperationException("mesh query parameter is required."),
                cancellationToken);
            return Results.Ok(peers.Select(ToPeerRow).ToArray());
        });
        app.MapPost("/locations/peers/wait", async (
            PeerLocationWaitReq request,
            IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var deadline = DateTimeOffset.UtcNow + timeout;
            PeerLocationRow[] rows = [];
            while (DateTimeOffset.UtcNow < deadline)
            {
                rows = (await query.ListMeshNodeDescriptorsAsync(
                        request.MeshName,
                        cancellationToken))
                    .Select(ToPeerRow)
                    .ToArray();
                var matches = rows.Where(row =>
                        row.Role == request.Role
                        && row.NodeRid == request.NodeRid
                        && (request.Weight is null || row.Weight == request.Weight))
                    .ToArray();
                var reached = request.Present
                    ? matches.Length == 1
                      && (request.Endpoint is null || matches[0].Endpoint == request.Endpoint)
                    : matches.Length == 0;
                if (reached) return Results.Ok(rows);

                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            }

            return Results.Problem(
                $"Peer row did not reach the requested state for rid={request.NodeRid}.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        // Member-peer user surface check: cached resolver read with Refresh
        // freshness (doc §1 verification basis).
        app.MapGet("/locations/member-peers", async (
            string? mesh,
            IZLinkMeshNodeLocationResolver resolver,
            CancellationToken cancellationToken) =>
        {
            var peers = await resolver.ListLiveMeshNodesAsync(
                mesh ?? throw new InvalidOperationException("mesh query parameter is required."),
                cancellationToken);
            return Results.Ok(peers.Select(ToPeerRow).ToArray());
        });
        app.MapGet("/locations/status", async (
            IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var status = await query.GetStatusAsync(cancellationToken);
            return Results.Ok(new LocationStatusRes(
                status.StoreHealthy,
                status.OwnerLeaseHealthy));
        });
        app.MapPost("/profile/request", async (
            ProfileReq request,
            IZLinkChannelClient channel,
            CancellationToken cancellationToken) =>
        {
            var reply = await channel.RequestToChannel("profile", request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ProfileRes>(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/manual", async (
            ProfileReq request,
            IZLinkChannelClient channel,
            CancellationToken cancellationToken) =>
        {
            var reply = await channel.RequestToChannel("profile.manual", request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ProfileRes>(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/command", (
            ProfileMsg command,
            IZLinkChannelClient channel,
            CancellationToken cancellationToken) =>
        {
            channel.SendToChannel("profile", command).TrySubmit();
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/route/request", async (
            ScenarioRoutePing request,
            IZLinkRouteClient route,
            CancellationToken cancellationToken) =>
        {
            var reply = await route.RequestToNode("profile.route", RoutingId.From("api-b"), request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ScenarioRoutePong>(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/route/missing", async (
            ScenarioRoutePing request,
            IZLinkRouteClient route) =>
        {
            try
            {
                await route.RequestToNode("profile.route", RoutingId.From("missing-rid"), request)
                    .Timeout(TimeSpan.FromMilliseconds(300))
                    .Async<ScenarioRoutePong>();
            }
            catch (ZLinkFrameworkException error) when (
                error.Kind == ZLinkFrameworkErrorKind.RequestTargetNotFound)
            {
                return Results.Ok(new ExpectedFailureRes(error.Kind.ToString()));
            }

            throw new InvalidOperationException(
                "A request to a missing route target completed without RequestTargetNotFound.");
        });
        app.MapPost("/profile/route/target", async (
            TargetedRoutePing request,
            IZLinkRouteClient route,
            CancellationToken cancellationToken) =>
        {
            try
            {
                var reply = await route.RequestToNode(
                        "profile.route",
                        RoutingId.From(request.TargetRid),
                        new ScenarioRoutePing(request.Value))
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<ScenarioRoutePong>(cancellationToken);
                return Results.Ok(new ExpectedFailureRes($"UnexpectedReply:{reply.ProviderRid}"));
            }
            catch (ZLinkFrameworkException error) when (
                error.Kind is ZLinkFrameworkErrorKind.RequestTargetNotFound
                    or ZLinkFrameworkErrorKind.RouteNotConnected)
            {
                return Results.Ok(new ExpectedFailureRes(error.Kind.ToString()));
            }
            catch (TimeoutException)
            {
                return Results.Ok(new ExpectedFailureRes("Timeout"));
            }
        });
        app.MapPost("/admin/drain", async (
            IZLinkDrainControl drain,
            CancellationToken cancellationToken) =>
        {
            var result = await drain.DrainAsync(TimeSpan.FromSeconds(30), cancellationToken);
            return Results.Ok(result switch
            {
                Drained => new DrainResultRes(nameof(Drained)),
                ForceStopped forced => new DrainResultRes(nameof(ForceStopped), forced.Reason.ToString()),
                _ => throw new InvalidOperationException($"Unknown drain result '{result.GetType().Name}'.")
            });
        });
        app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
        {
            evidence.Clear();
            return Results.Ok(new { status = "cleared" });
        });
        app.MapPost("/evidence/wait", async (
            EvidenceWaitReq request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            try
            {
                var snapshot = await evidence.WaitUntilAsync(
                    line => line.Contains(request.Contains, StringComparison.Ordinal),
                    timeout,
                    cancellationToken);
                return Results.Ok(snapshot);
            }
            catch (TimeoutException error)
            {
                return Results.Problem(
                    error.Message,
                    statusCode: StatusCodes.Status504GatewayTimeout);
            }
        });
        app.MapPost("/evidence/wait-count", async (
            EvidenceCountWaitReq request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 120000));
            try
            {
                var snapshot = await evidence.WaitUntilCountAsync(
                    request.Contains,
                    Math.Max(1, request.MinimumCount),
                    timeout,
                    cancellationToken);
                return Results.Ok(snapshot);
            }
            catch (TimeoutException error)
            {
                return Results.Problem(
                    error.Message,
                    statusCode: StatusCodes.Status504GatewayTimeout);
            }
        });
        app.MapPost("/evidence/wait-quiet", async (
            EvidenceQuietWaitReq request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var quietPeriod = TimeSpan.FromMilliseconds(Math.Clamp(request.QuietMilliseconds, 1, 5000));
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 60000));
            try
            {
                var snapshot = await evidence.WaitUntilQuietAsync(
                    request.Contains,
                    quietPeriod,
                    timeout,
                    cancellationToken);
                return Results.Ok(snapshot);
            }
            catch (TimeoutException error)
            {
                return Results.Problem(
                    error.Message,
                    statusCode: StatusCodes.Status504GatewayTimeout);
            }
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
    }

    private static PeerLocationRow ToPeerRow(ZLinkMeshNodeDescriptor peer)
    {
        return new PeerLocationRow(
            peer.MeshName,
            peer.Rid.ToString(),
            "Router",
            peer.Endpoint,
            (uint)(peer.ChannelWeights.TryGetValue(peer.MeshName, out var weight) ? weight : 0),
            peer.OwnerId,
            peer.LifecycleGeneration);
    }

}
