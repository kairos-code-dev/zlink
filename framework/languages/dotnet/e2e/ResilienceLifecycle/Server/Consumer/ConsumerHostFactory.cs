using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ResilienceLifecycle.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;

using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Locations.Redis;

namespace ResilienceLifecycle.Server.Consumer;

using Zlink.Framework.E2E.Configuration;

internal static class ConsumerHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ConsumerOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix)));
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, "consumer-flow.log"))
                .TraceLabel("consumer");
            framework.AddClientServerChannel(ResilienceLifecycleNames.Channel).EnableClient();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
        // Topology waits observe the peer location list — the operational
        // surface the scenarios verify recovery against (config-5 §3).
        app.MapPost("/topology/wait", async (
            IZLinkLocationRuntimeQuery query,
            TopologyWaitReq request) =>
        {
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromMilliseconds(request.TimeoutMilliseconds);
            while (true)
            {
                var peers = await query.ListPeerLocationsAsync(new ZLinkPeerLocationFilter());
                var matches = peers
                    .Where(peer => peer.NodeRid is { Size: > 0 } rid
                                   && rid == RoutingId.From(request.RoutingId)
                                   && (request.ExpectedWeight is null
                                       || peer.Weight == request.ExpectedWeight))
                    .ToArray();
                var satisfied = request.ExpectedCount == 0
                    ? matches.Length == 0
                    : matches.Length >= request.ExpectedCount;
                if (satisfied)
                    return Results.Ok(matches
                        .Select(peer => new TopologyEntryRes(
                            peer.NodeRid?.ToString(),
                            peer.Endpoint,
                            "Ready",
                            peer.Weight))
                        .ToArray());

                if (DateTimeOffset.UtcNow >= deadline)
                    return Results.Problem(
                        $"Topology wait for '{request.RoutingId}' (expected {request.ExpectedCount}) timed out.");

                await Task.Delay(150);
            }
        });

        app.MapPost("/profile/request", async (
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/timeout/{milliseconds:int}", async (
            int milliseconds,
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(ResilienceLifecycleNames.Channel, request)
                    .Timeout(TimeSpan.FromMilliseconds(milliseconds))
                    .Async<ProfileRes>();
                return Results.Ok(reply);
            }
            catch (TimeoutException)
            {
                return Results.StatusCode(StatusCodes.Status408RequestTimeout);
            }
        });
        app.MapPost("/profile/request/attempt/{milliseconds:int}", async (
            int milliseconds,
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(ResilienceLifecycleNames.Channel, request)
                    .Timeout(TimeSpan.FromMilliseconds(milliseconds))
                    .Async<ProfileRes>();
                return new ProfileAttemptRes(reply, null, false);
            }
            catch (TimeoutException)
            {
                return new ProfileAttemptRes(null, nameof(TimeoutException), true);
            }
            catch (ZLinkFrameworkException error)
            {
                return new ProfileAttemptRes(null, error.Kind.ToString(), error.IsRetriable);
            }
        });
        app.MapPost("/profile/request/missing", async (
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(
                        ResilienceLifecycleNames.Channel,
                        new MissingProfileReq(request.Value, request.Marker))
                    .Timeout(TimeSpan.FromSeconds(3))
                    .Async<ProfileRes>();
                return Results.Ok(reply);
            }
            catch (Exception ex)
            {
                return Results.Problem(ex.Message);
            }
        });
        app.MapPost("/profile/command", (
            ProfileMsg command,
            IZLinkChannelClient channel) =>
        {
            channel.SendToChannel(ResilienceLifecycleNames.Channel, command).Submit();
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/request/new-client", async (ProfileReq request) =>
        {
            using var host = CreateClientHost(options, $"storm-{request.Marker}");
            await host.StartAsync();
            try
            {
                var channel = host.Services.GetRequiredService<IZLinkChannelClient>();
                var reply = await RequestProfileAsync(channel, request);
                return Results.Ok(reply);
            }
            finally
            {
                await StopClientHostAsync(host);
            }
        });
        return app;
    }

    static IHost CreateClientHost(ConsumerOptions options, string traceLabel)
    {
        return Host.CreateDefaultBuilder()
            .ConfigureServices(services =>
            {
                services.AddZLinkFramework(framework =>
                {
                    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                        .SetConnectionString(options.RedisEndpoint)
                        .SetKeyPrefix(options.RedisKeyPrefix)));
                    framework.ConfigureDispatch()
                        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                        .TraceLogFile(Path.Combine(options.LogDir, $"{traceLabel}-flow.log"))
                        .TraceLabel(traceLabel);
                    framework.AddClientServerChannel(ResilienceLifecycleNames.Channel).EnableClient();
                });
            })
            .Build();
    }

    static async Task StopClientHostAsync(IHost host)
    {
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        try
        {
            await host.StopAsync(cts.Token);
        }
        catch (OperationCanceledException)
        {
            // A reconnect storm scenario must not hang the HTTP response while host shutdown waits.
        }
    }

    static async Task<ProfileRes> RequestProfileAsync(
        IZLinkChannelClient channel,
        ProfileReq request)
        => await channel.RequestToChannel(ResilienceLifecycleNames.Channel, request)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileRes>();
}

internal sealed record ConsumerOptions(
    string HttpUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string LogDir)
{
    public static ConsumerOptions Parse(string[] args)
        => E2eConfiguration.Load<ConsumerOptions>(args);
}

internal sealed record TopologyEntryRes(string? RoutingId, string Endpoint, string State, uint Weight);
