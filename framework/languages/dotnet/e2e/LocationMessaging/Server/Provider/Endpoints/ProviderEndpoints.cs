using LocationMessaging.Server.Provider.Configuration;
using LocationMessaging.Server.Provider.Infrastructure;
using LocationMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
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
            var peers = await query.ListPeerLocationsAsync(
                new ZLinkPeerLocationFilter(MeshName: mesh),
                cancellationToken);
            return Results.Ok(peers.Select(ToPeerRow).ToArray());
        });
        // Member-peer user surface check: cached resolver read with Refresh
        // freshness (doc §1 verification basis).
        app.MapGet("/locations/member-peers", async (
            string? mesh,
            IZLinkPeerLocationResolver resolver,
            CancellationToken cancellationToken) =>
        {
            var peers = await resolver.ListLivePeersAsync(
                new ZLinkPeerLocationFilter(MeshName: mesh),
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
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, "profile", request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/manual", async (
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, "profile.manual", request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/command", async (
            ProfileMsg command,
            IZLinkChannelClient channel) =>
        {
            await SendProfileWithRetryAsync(channel, "profile", command);
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/route/request", async (
            ScenarioRoutePing request,
            IZLinkRouteClient route) =>
        {
            var reply = await RequestRouteWithRetryAsync(route, RoutingId.From("api-b"), request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/route/missing", async (
            ScenarioRoutePing request,
            IZLinkRouteClient route) =>
        {
            var failed = false;
            try
            {
                await route.RequestToNode("profile.route", RoutingId.From("missing-rid"), request)
                    .Timeout(TimeSpan.FromMilliseconds(300))
                    .Async<ScenarioRoutePong>();
            }
            catch (Exception)
            {
                failed = true;
            }

            return Results.Ok(new RouteMissingRes(failed));
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
            var snapshot = await evidence.WaitUntilAsync(
                line => line.Contains(request.Contains, StringComparison.Ordinal),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
    }

    private static async Task<ProfileRes> RequestProfileWithRetryAsync(
        IZLinkChannelClient channel,
        string channelName,
        ProfileReq request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
            try
            {
                return await channel.RequestToChannel(channelName, request)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<ProfileRes>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }

        throw new InvalidOperationException("Timed out waiting for profile request channel route.", last);
    }

    private static async Task SendProfileWithRetryAsync(
        IZLinkChannelClient channel,
        string channelName,
        ProfileMsg command)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
            try
            {
                channel.SendToChannel(channelName, command).Submit();
                return;
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }

        throw new InvalidOperationException("Timed out waiting for profile send channel route.", last);
    }

    private static PeerLocationRow ToPeerRow(ZLinkPeerLocation peer)
    {
        return new PeerLocationRow(
            peer.MeshName,
            peer.NodeRid?.ToString(),
            peer.Role.ToString(),
            peer.Endpoint,
            peer.Weight,
            peer.OwnerId,
            peer.Generation);
    }

    private static bool IsRetriableRequestStartupFailure(ZLinkFrameworkException ex)
    {
        return ex.IsRetriable
               || ex.Kind is ZLinkFrameworkErrorKind.RouteNotConnected
                   or ZLinkFrameworkErrorKind.RequestTargetNotFound
                   or ZLinkFrameworkErrorKind.RequestProtocolError;
    }

    private static async Task<ScenarioRoutePong> RequestRouteWithRetryAsync(
        IZLinkRouteClient route,
        RoutingId target,
        ScenarioRoutePing request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
            try
            {
                return await route.RequestToNode("profile.route", target, request)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<ScenarioRoutePong>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }

        throw new InvalidOperationException("Timed out waiting for route mesh target.", last);
    }
}
