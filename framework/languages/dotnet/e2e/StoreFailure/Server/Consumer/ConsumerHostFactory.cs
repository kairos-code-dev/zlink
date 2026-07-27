using System.Diagnostics;
using Microsoft.Extensions.Configuration;

using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using StoreFailure.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;

using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Locations.Redis;

namespace StoreFailure.Server.Consumer;

using Zlink.Framework.E2E.Configuration;

internal static class ConsumerHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ConsumerOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
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
                "delay" => new DelayableLocationStore(redisStore, delayState),
                _ => redisStore
            };
            framework.AddLocationStore(store);
            var locations = framework.ConfigureLocations();
            locations.OwnerLeaseRenewInterval = TimeSpan.FromMilliseconds(options.LocationHeartbeatMs);
            locations.OwnerLeaseTtl = TimeSpan.FromMilliseconds(options.LocationLeaseTtlMs);
            // The E2E compresses the lease TTL to three seconds. Scale the
            // renewal attempt bound with the heartbeat as well; retaining
            // the production three-second default would consume the whole
            // lease before the failure could become observable.
            locations.OwnerLeaseRenewTimeout = TimeSpan.FromMilliseconds(
                Math.Max(100, options.LocationHeartbeatMs / 2));
            locations.PollingInterval = TimeSpan.FromMilliseconds(options.LocationPollingMs);
            locations.StoreFailureGrace = TimeSpan.FromMilliseconds(options.LocationGraceMs);
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.TraceLabel}-flow.log"))
                .TraceLabel(options.TraceLabel);
            // SF-A2 starts a second consumer against the same RouteMesh. Its
            // trace label is also its stable harness identity, so stopping
            // that observer cannot hand over and then remove the primary
            // consumer's descriptor under the same routing ID.
            JoinConsumerMesh(framework, options.TraceLabel);
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
                var peers = await query.ListMeshNodeDescriptorsAsync(
                    StoreFailureNames.Channel,
                    cancellationToken);
                return Results.Ok(peers
                    .Select(peer => new PeerRowRes(
                        peer.Rid.ToString(),
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
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 60000));
            var elapsed = Stopwatch.StartNew();
            while (elapsed.Elapsed < timeout)
            {
                try
                {
                    var rows = (await query.ListMeshNodeDescriptorsAsync(
                            StoreFailureNames.Channel, cancellationToken))
                        .Select(peer => new PeerRowRes(
                            peer.Rid.ToString(), peer.Endpoint, peer.OwnerId, peer.Draining))
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
        app.MapPost("/query/routes/wait", async (
            RouteReadyWaitReq request,
            IZLinkRouteMeshRuntime runtime,
            ILoggerFactory loggerFactory,
            CancellationToken cancellationToken) =>
        {
            var logger = loggerFactory.CreateLogger("StoreFailure.RouteProbe");
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 60000));
            var elapsed = Stopwatch.StartNew();
            while (elapsed.Elapsed < timeout)
            {
                var current = runtime.Snapshot(StoreFailureNames.Channel);
                var readyMembers = current.Channels
                    .Where(channel => channel.ChannelName == StoreFailureNames.Channel)
                    .Select(channel => channel.ReadyMemberCount)
                    .DefaultIfEmpty()
                    .Max();
                var reached = readyMembers >= request.MinimumReadyMembers
                              && request.ReadyRids.All(rid => current.Peers.Any(peer =>
                                  peer.Rid.ToString() == rid && peer.Ready))
                              && request.NotReadyRids.All(rid => current.Peers.All(peer =>
                                  peer.Rid.ToString() != rid || !peer.Ready));
                if (reached)
                    return Results.Ok(new RouteReadyRes(readyMembers));

                await Task.Delay(TimeSpan.FromMilliseconds(50), cancellationToken);
            }

            var snapshot = runtime.Snapshot(StoreFailureNames.Channel);
            logger.LogWarning(
                "Route readiness wait expired mesh={MeshName} localRid={LocalRid} state={State} " +
                "peers={Peers} channels={Channels}",
                snapshot.MeshName,
                snapshot.Rid,
                snapshot.State,
                DescribePeers(snapshot),
                DescribeChannels(snapshot));

            return Results.Problem("Route did not reach the requested ready-member count.",
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
            IZLinkRouteClient channel) =>
        {
            var reply = await RequestProfileAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/timeout/{milliseconds:int}", async (
            int milliseconds,
            ProfileReq request,
            IZLinkRouteClient channel,
            IZLinkRouteMeshRuntime runtime,
            ILoggerFactory loggerFactory) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(
                    StoreFailureNames.Channel, StoreFailureNames.Channel, request)
                    .Timeout(TimeSpan.FromMilliseconds(milliseconds))
                    .Async<ProfileRes>();
                return Results.Ok(reply);
            }
            catch (TimeoutException)
            {
                return Results.StatusCode(StatusCodes.Status408RequestTimeout);
            }
            catch (Exception error)
            {
                var snapshot = runtime.Snapshot(StoreFailureNames.Channel);
                loggerFactory.CreateLogger("StoreFailure.RequestProbe").LogError(
                    error,
                    "Profile request failed marker={Marker} peers={Peers} channels={Channels}",
                    request.Marker,
                    DescribePeers(snapshot),
                    DescribeChannels(snapshot));
                throw;
            }
        });
        app.MapPost("/profile/request/missing", async (
            ProfileReq request,
            IZLinkRouteClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(
                    StoreFailureNames.Channel, StoreFailureNames.Channel, request)
                    .Timeout(TimeSpan.FromSeconds(3))
                    .Async<ProfileRes>();
                return Results.Ok(reply);
            }
            catch (Exception ex)
            {
                return Results.Problem(ex.Message);
            }
        });
        app.MapPost("/profile/command", async (
            ProfileMsg command,
            IZLinkRouteClient channel,
            CancellationToken cancellationToken) =>
        {
            await channel.SendToChannel(
                    StoreFailureNames.Channel, StoreFailureNames.Channel, command)
                .SubmitAsync(cancellationToken);
            return Results.Ok(new { status = "sent" });
        });
        app.MapPost("/profile/request/new-client", async (ProfileReq request) =>
        {
            using var host = CreateClientHost(options, $"storm-{request.Marker}");
            await host.StartAsync();
            try
            {
                var channel = host.Services.GetRequiredService<IZLinkRouteClient>();
                var reply = await RequestProfileAsync(channel, request);
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
            .ConfigureAppConfiguration((_, configuration) => configuration.Sources.Clear())
            .ConfigureServices(services =>
            {
                services.AddZLinkFramework(framework =>
                {
                    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                        .SetConnectionString(options.RedisEndpoint)
                        .SetKeyPrefix(options.RedisKeyPrefix)));
                    framework.ConfigureDispatch()
                        .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                        .TraceLogFile(Path.Combine(options.LogDir, $"{traceLabel}-flow.log"))
                        .TraceLabel(traceLabel);
                    JoinConsumerMesh(framework, $"consumer-{traceLabel}");
                });
            })
            .Build();
    }

    // A caller joins the providers' RouteMesh with its own membership and
    // issues ChannelName select-one calls through IZLinkRouteClient (spec 10
    // §1). The bind uses an ephemeral port; the rid is fixed per harness role
    // because the store decorators under test do not provide the routing-id
    // slot-allocation capability.
    static void JoinConsumerMesh(IZLinkFrameworkOptions framework, string rid)
    {
        var mesh = framework.AddRouteMesh(StoreFailureNames.Channel)
            .Listen("tcp://127.0.0.1:0")
            .SetRoutingId(RoutingId.From(rid));
        mesh.ChannelName(StoreFailureNames.ConsumerChannel);
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
        IZLinkRouteClient channel,
        ProfileReq request)
        => await channel.RequestToChannel(
                    StoreFailureNames.Channel, StoreFailureNames.Channel, request)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileRes>();

    static string DescribePeers(ZLinkMeshNodeSnapshot snapshot) =>
        string.Join(";", snapshot.Peers.Select(peer =>
            $"{peer.Rid}|ready={peer.Ready}|admission={peer.AdmissionState}|" +
            $"channels={string.Join(',', peer.ChannelNames)}|failure={peer.LastFailure}"));

    static string DescribeChannels(ZLinkMeshNodeSnapshot snapshot) =>
        string.Join(";", snapshot.Channels.Select(channel =>
            $"{channel.ChannelName}|ready={channel.ReadyMemberCount}|selectable={channel.Selectable}"));
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
        => E2eConfiguration.Load<ConsumerOptions>(args);
}
