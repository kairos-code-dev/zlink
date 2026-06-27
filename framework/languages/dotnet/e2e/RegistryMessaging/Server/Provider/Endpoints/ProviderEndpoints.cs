using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Hosting;
using RegistryMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Registry;
using RegistryMessaging.Server.Provider.Configuration;
using RegistryMessaging.Server.Provider.Handlers;
using RegistryMessaging.Server.Provider.Infrastructure;
using RegistryMessaging.Server.Provider;

namespace RegistryMessaging.Server.Provider.Endpoints;

internal static class ProviderEndpoints
{
    public static void MapProviderEndpoints(this WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/profile/request", async (
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, "profile", request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/manual", async (
            ProfileRequest request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, "profile.manual", request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/command", async (
            ProfileCommand command,
            IZLinkChannelClient channel) =>
        {
            await SendProfileWithRetryAsync(channel, "profile", command);
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/workflow/request", async (
            WorkflowRequest request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestWorkflowWithRetryAsync(channel, request);
            return Results.Ok(reply);
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
                await route.Request("profile.route", RoutingId.From("missing-rid"), request)
                    .PacketName("ScenarioRoutePing")
                    .Timeout(TimeSpan.FromMilliseconds(300))
                    .Async<ScenarioRoutePong>();
            }
            catch (Exception)
            {
                failed = true;
            }

            return Results.Ok(new RouteMissingResult(failed));
        });
        app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
        {
            evidence.Clear();
            return Results.Ok(new { status = "cleared" });
        });
        app.MapPost("/evidence/wait", async (
            EvidenceWaitRequest request,
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

    static async Task<ProfileReply> RequestProfileWithRetryAsync(
        IZLinkChannelClient channel,
        string channelName,
        ProfileRequest request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel(channelName, request)
                    .PacketName("ProfileRequest")
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<ProfileReply>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for profile request channel route.", last);
    }

    static async Task SendProfileWithRetryAsync(
        IZLinkChannelClient channel,
        string channelName,
        ProfileCommand command)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                await channel.SendToChannel(channelName, command)
                    .PacketName("ProfileCommand")
                    .Async();
                return;
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for profile send channel route.", last);
    }

    static bool IsRetriableRequestStartupFailure(ZLinkFrameworkException ex) =>
        ex.IsRetriable
        || ex.Kind is ZLinkFrameworkErrorKind.RouteNotConnected
            or ZLinkFrameworkErrorKind.RequestTargetNotFound
            or ZLinkFrameworkErrorKind.RequestProtocolError;

    static async Task<WorkflowReply> RequestWorkflowWithRetryAsync(
        IZLinkChannelClient channel,
        WorkflowRequest request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel("workflow", request)
                    .PacketName("WorkflowRequest")
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<WorkflowReply>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for workflow request channel route.", last);
    }

    static async Task<ScenarioRoutePong> RequestRouteWithRetryAsync(
        IZLinkRouteClient route,
        RoutingId target,
        ScenarioRoutePing request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await route.Request("profile.route", target, request)
                    .PacketName("ScenarioRoutePing")
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<ScenarioRoutePong>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for route mesh target.", last);
    }
}
