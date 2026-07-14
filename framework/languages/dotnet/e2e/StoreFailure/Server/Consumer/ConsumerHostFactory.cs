using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using StoreFailure.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;

using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Locations.Redis;

namespace StoreFailure.Server.Consumer;

internal static class ConsumerHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ConsumerOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        var delayState = new LocationStoreDelayState();
        builder.Services.AddSingleton(delayState);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddZLinkFramework(framework =>
        {
            var redisStore = new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix));
            // store-mode polling hides the change-stamp surface, forcing
            // the pure polling path (SF-A2: polling is the correctness
            // path; the stamp is only a latency optimization).
            IZLinkLocationStore store = options.StoreMode switch
            {
                "polling" => new PollingOnlyLocationStore(redisStore),
                "delay" => new DelayableLocationStore(redisStore, delayState, redisStore),
                _ => redisStore
            };
            framework.AddLocationStore(store);
            var locations = framework.ConfigureLocations();
            locations.HeartbeatInterval = TimeSpan.FromMilliseconds(options.LocationHeartbeatMs);
            locations.OwnerLeaseTtl = TimeSpan.FromMilliseconds(options.LocationLeaseTtlMs);
            locations.PollingInterval = TimeSpan.FromMilliseconds(options.LocationPollingMs);
            locations.StoreFailureGrace = TimeSpan.FromMilliseconds(options.LocationGraceMs);
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.TraceLabel}-flow.log"))
                .TraceLabel(options.TraceLabel);
            framework.AddClientServerChannel(StoreFailureNames.Channel).EnableClient();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        app.MapGet("/query/status", async (
            IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var status = await query.GetStatusAsync(cancellationToken);
            return Results.Ok(new RuntimeStatusRes(
                status.StoreHealthy,
                status.WatchEnabled,
                status.LastError,
                status.OwnerLeaseHealthy,
                status.OwnerLeaseRenewedAt,
                status.LastRefreshAt));
        });
        app.MapGet("/query/peers", async (
            IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            try
            {
                var peers = await query.ListPeerLocationsAsync(
                    new ZLinkPeerLocationFilter(),
                    cancellationToken);
                return Results.Ok(peers
                    .Select(peer => new PeerRowRes(
                        peer.NodeRid?.ToString(),
                        peer.Endpoint,
                        peer.OwnerId,
                        peer.Draining))
                    .ToArray());
            }
            catch (Exception error)
            {
                // A dead store makes the raw row list unavailable; the
                // probe treats 503 as "store unreachable".
                return Results.Problem(error.Message, statusCode: StatusCodes.Status503ServiceUnavailable);
            }
        });
        app.MapPost("/query/peers/wait", async (
            PeerRowsWaitReq request,
            IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var deadline = DateTimeOffset.UtcNow
                           + TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 60000));
            while (DateTimeOffset.UtcNow < deadline)
            {
                try
                {
                    var rows = (await query.ListPeerLocationsAsync(
                            new ZLinkPeerLocationFilter(), cancellationToken))
                        .Select(peer => new PeerRowRes(
                            peer.NodeRid?.ToString(), peer.Endpoint, peer.OwnerId, peer.Draining))
                        .ToArray();
                    var reached = request.PresentRids.All(rid => rows.Any(row => row.Rid == rid))
                                  && request.AbsentRids.All(rid => rows.All(row => row.Rid != rid))
                                  && request.DrainingRids.All(rid =>
                                      rows.Any(row => row.Rid == rid && row.Draining));
                    if (reached) return Results.Ok(rows);
                }
                catch
                {
                    // A store outage is a transient observation while this bounded wait is active.
                }

                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            }

            return Results.Problem("Peer rows did not reach the requested state.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/query/status/wait", async (
            RuntimeStatusWaitReq request,
            IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var response = await RuntimeStatusWaiter.WaitAsync(async token =>
            {
                var status = await query.GetStatusAsync(token);
                return new RuntimeStatusRes(
                    status.StoreHealthy, status.WatchEnabled, status.LastError,
                    status.OwnerLeaseHealthy, status.OwnerLeaseRenewedAt, status.LastRefreshAt);
            }, request, cancellationToken);
            if (response is not null) return Results.Ok(response);

            return Results.Problem("Runtime status did not reach the requested state.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/profile/request", async (
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestProfileWithRetryAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/timeout/{milliseconds:int}", async (
            int milliseconds,
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(StoreFailureNames.Channel, request)
                    .Timeout(TimeSpan.FromMilliseconds(milliseconds))
                    .Async<ProfileRes>();
                return Results.Ok(reply);
            }
            catch (TimeoutException)
            {
                return Results.StatusCode(StatusCodes.Status408RequestTimeout);
            }
        });
        app.MapPost("/profile/request/missing", async (
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(StoreFailureNames.Channel, request)
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
            channel.SendToChannel(StoreFailureNames.Channel, command).Submit();
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/request/new-client", async (ProfileReq request) =>
        {
            using var host = CreateClientHost(options, $"storm-{request.Marker}");
            await host.StartAsync();
            try
            {
                var channel = host.Services.GetRequiredService<IZLinkChannelClient>();
                var reply = await RequestProfileWithRetryAsync(channel, request);
                return Results.Ok(reply);
            }
            finally
            {
                await StopClientHostAsync(host);
            }
        });
        app.MapPost("/admin/store-delay", (StoreDelayReq request, LocationStoreDelayState state) =>
        {
            state.SetDelay(TimeSpan.FromMilliseconds(request.Milliseconds));
            return Results.Ok(new { delayMilliseconds = state.DelayMilliseconds });
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
                    framework.AddClientServerChannel(StoreFailureNames.Channel).EnableClient();
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

    static async Task<ProfileRes> RequestProfileWithRetryAsync(
        IZLinkChannelClient channel,
        ProfileReq request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel(StoreFailureNames.Channel, request)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<ProfileRes>();
            }
            catch (Exception ex) when (
                ex is TimeoutException
                || (ex is ZLinkFrameworkException framework && IsRetriableStartupFailure(framework)))
            {
                // A request that rode a dying connection times out inside
                // the down window; the next attempt takes the surviving
                // provider once reconcile drops the dead link.
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for resilience profile providers.", last);
    }

    static bool IsRetriableStartupFailure(ZLinkFrameworkException ex) =>
        ex.IsRetriable
        || ex.Kind is ZLinkFrameworkErrorKind.RouteNotConnected
            or ZLinkFrameworkErrorKind.RequestTargetNotFound
            or ZLinkFrameworkErrorKind.RequestProtocolError;
}

internal sealed record ConsumerOptions(
    string HttpUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string LogDir,
    string TraceLabel,
    string StoreMode,
    int LocationHeartbeatMs,
    int LocationLeaseTtlMs,
    int LocationPollingMs,
    int LocationGraceMs)
{
    public static ConsumerOptions Parse(string[] args)
    {
        var values = ParseArgs(args);
        return new ConsumerOptions(
            Require(values, "http-url"),
            Require(values, "redis-endpoint"),
            Require(values, "redis-key-prefix"),
            Require(values, "log-dir"),
            values.TryGetValue("trace-label", out var label) ? label : "consumer",
            values.TryGetValue("store-mode", out var mode) ? mode : "stamp",
            values.TryGetValue("location-heartbeat-ms", out var hb) ? int.Parse(hb) : 1000,
            values.TryGetValue("location-lease-ttl-ms", out var ttl) ? int.Parse(ttl) : 3000,
            values.TryGetValue("location-polling-ms", out var poll) ? int.Parse(poll) : 500,
            values.TryGetValue("location-grace-ms", out var grace) ? int.Parse(grace) : 6000);
    }

    static IReadOnlyDictionary<string, string> ParseArgs(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}.");
            }

            values[key[2..]] = args[++i];
        }

        return values;
    }

    static string Require(IReadOnlyDictionary<string, string> values, string key) =>
        values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");
}
