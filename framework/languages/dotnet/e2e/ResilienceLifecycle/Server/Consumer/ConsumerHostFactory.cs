using Microsoft.Extensions.Configuration;

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
using Zlink.Framework.Contracts.Eventing;

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
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton<ConnectionEvidence>();
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, ConnectionEventObserver>();
        builder.Services.AddSingleton(new StormClientFleet(options));
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
        builder.Services.AddZLinkMonitoring(monitor => monitor.AddSocketEvents(
            "resilience.profile.client",
            ZLinkSocketEventKind.ConnectionReady,
            ZLinkSocketEventKind.Disconnected));

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
        app.MapGet("/connections", (ConnectionEvidence evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/connections/wait", async (
            ConnectionWaitReq request,
            ConnectionEvidence evidence,
            CancellationToken cancellationToken) => Results.Ok(await evidence.WaitAsync(
                request.ContainsAll,
                request.AfterCount,
                TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000)),
                cancellationToken)));
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
                                       || peer.Weight == request.ExpectedWeight)
                                   && (request.ExpectedDraining is null
                                       || peer.Draining == request.ExpectedDraining))
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
                            peer.Weight,
                            peer.Generation,
                            peer.Draining))
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
        app.MapPost("/storm/start", async (
            ProfileReq request,
            StormClientFleet fleet,
            CancellationToken cancellationToken) => Results.Ok(
                await fleet.StartAndRequestAsync(request.Marker, cancellationToken)));
        app.MapPost("/storm/request-all", async (
            ProfileReq request,
            StormClientFleet fleet,
            CancellationToken cancellationToken) => Results.Ok(
                await fleet.RequestAllAsync(request.Marker, cancellationToken)));
        app.MapGet("/storm/connections/count", (StormClientFleet fleet) => Results.Ok(fleet.ConnectionCount));
        app.MapPost("/storm/wait-ready", async (
            ConnectionWaitReq request,
            StormClientFleet fleet,
            CancellationToken cancellationToken) => Results.Ok(
                await fleet.WaitReadyAsync(request.AfterCount, cancellationToken)));
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

internal sealed record TopologyEntryRes(
    string? RoutingId,
    string Endpoint,
    string State,
    uint Weight,
    long Generation,
    bool Draining);

internal sealed class ConnectionEvidence
{
    private readonly System.Collections.Concurrent.ConcurrentQueue<string> _entries = new();
    private readonly SemaphoreSlim _changed = new(0);

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        _changed.Release();
    }

    public async Task<string[]> WaitAsync(
        IReadOnlyCollection<string> required,
        int afterCount,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var snapshot = _entries.ToArray();
            if (snapshot.Skip(Math.Clamp(afterCount, 0, snapshot.Length)).Any(line =>
                    required.All(expected => line.Contains(expected, StringComparison.Ordinal)))) return snapshot;

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero
                || !await _changed.WaitAsync(remaining, cancellationToken))
                throw new TimeoutException("Expected connection evidence did not arrive.");
        }
    }

    public async Task<string[]> WaitAllAsync(
        IReadOnlyCollection<string> requiredEvents,
        int afterCount,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var snapshot = _entries.ToArray();
            var added = snapshot.Skip(Math.Clamp(afterCount, 0, snapshot.Length)).ToArray();
            if (requiredEvents.All(expected => added.Any(line =>
                    line.Contains(expected, StringComparison.Ordinal)))) return snapshot;

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero
                || !await _changed.WaitAsync(remaining, cancellationToken))
                throw new TimeoutException("Expected connection evidence did not arrive.");
        }
    }


    public string[] Snapshot() => _entries.ToArray();
}

internal sealed class ConnectionEventObserver(
    ConnectionEvidence evidence,
    ILogger<ConnectionEventObserver> logger)
    : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var entry =
            $"monitor-socket|source={@event.SourceName}|kind={@event.Event}"
            + $"|remote={@event.RemoteAddr}|routing={@event.RoutingId}";
        evidence.Add(entry);
        logger.LogInformation("resilience connection evidence: {Entry}", entry);
        return ValueTask.CompletedTask;
    }
}
