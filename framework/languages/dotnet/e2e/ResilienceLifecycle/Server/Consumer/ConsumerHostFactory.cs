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
using Zlink.Framework.Contracts.Configuration;
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
            JoinConsumerMesh(framework, "consumer");
        });
        // Mesh peers replace the 9.x socket monitor as the connection-evidence
        // source: a peer reaching ready is the wire-level ConnectionReady and a
        // ready peer dropping out is its Disconnected.
        builder.Services.AddHostedService(provider => new MeshConnectionObserverService(
            provider,
            provider.GetRequiredService<ConnectionEvidence>(),
            "monitor-socket|source=resilience.profile.client|"));

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
                var peers = await query.ListMeshNodeDescriptorsAsync(ResilienceLifecycleNames.Channel);
                var matches = peers
                    .Where(peer => peer.Rid == RoutingId.From(request.RoutingId)
                                   && (request.ExpectedWeight is null
                                       || (peer.ChannelWeights.TryGetValue(peer.MeshName, out var weight)
                                           ? weight : 0) == request.ExpectedWeight)
                                   && (request.ExpectedDraining is null
                                       || peer.Draining == request.ExpectedDraining))
                    .ToArray();
                var satisfied = request.ExpectedCount == 0
                    ? matches.Length == 0
                    : matches.Length >= request.ExpectedCount;
                if (satisfied)
                    return Results.Ok(matches
                        .Select(peer => new TopologyEntryRes(
                            peer.Rid.ToString(),
                            peer.Endpoint,
                            "Ready",
                            (uint)(peer.ChannelWeights.TryGetValue(peer.MeshName, out var weight)
                                ? weight : 0),
                            (long)peer.LifecycleGeneration,
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
            IZLinkRouteClient channel) =>
        {
            var reply = await RequestProfileAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/timeout/{milliseconds:int}", async (
            int milliseconds,
            ProfileReq request,
            IZLinkRouteClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(
                        ResilienceLifecycleNames.Channel, ResilienceLifecycleNames.Channel, request)
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
            IZLinkRouteClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(
                        ResilienceLifecycleNames.Channel, ResilienceLifecycleNames.Channel, request)
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
            IZLinkRouteClient channel) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(
                        ResilienceLifecycleNames.Channel,
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
            IZLinkRouteClient channel) =>
        {
            channel.SendToChannel(
                ResilienceLifecycleNames.Channel, ResilienceLifecycleNames.Channel, command).TrySubmit();
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
                    JoinConsumerMesh(framework, "newclient");
                });
            })
            .Build();
    }

    // A caller joins the providers' RouteMesh with its own membership and
    // issues ChannelName select-one calls through IZLinkRouteClient. The bind
    // uses an ephemeral port and the routing id comes from a store-allocated
    // group (spec 10 §1; fixed ids stay reserved for the providers).
    internal static void JoinConsumerMesh(IZLinkFrameworkOptions framework, string ridPrefix)
    {
        var mesh = framework.AddRouteMesh(ResilienceLifecycleNames.Channel)
            .Listen("tcp://127.0.0.1:0")
            .UseAllocatedRoutingId(slotCount: 128, routingIdPrefix: ridPrefix)
            .SetRoutingIdAllocationGroup($"resilience.{ridPrefix}");
        mesh.ChannelName(ResilienceLifecycleNames.ConsumerChannel);
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
                        ResilienceLifecycleNames.Channel, ResilienceLifecycleNames.Channel, request)
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
    // A pulse completed on every Add and swapped for a fresh one, so EVERY
    // concurrent waiter wakes — a counted semaphore hands one release to one
    // waiter and silently starves the rest.
    private readonly object _pulseGate = new();
    private TaskCompletionSource<bool> _pulse =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        TaskCompletionSource<bool> pulse;
        lock (_pulseGate)
        {
            pulse = _pulse;
            _pulse = new TaskCompletionSource<bool>(
                TaskCreationOptions.RunContinuationsAsynchronously);
        }

        pulse.TrySetResult(true);
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
            Task pulseTask;
            lock (_pulseGate)
            {
                pulseTask = _pulse.Task;
            }

            var snapshot = _entries.ToArray();
            if (snapshot.Skip(Math.Clamp(afterCount, 0, snapshot.Length)).Any(line =>
                    required.All(expected => line.Contains(expected, StringComparison.Ordinal)))) return snapshot;

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero
                || await Task.WhenAny(pulseTask, Task.Delay(remaining, cancellationToken)) != pulseTask)
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
            Task pulseTask;
            lock (_pulseGate)
            {
                pulseTask = _pulse.Task;
            }

            var snapshot = _entries.ToArray();
            var added = snapshot.Skip(Math.Clamp(afterCount, 0, snapshot.Length)).ToArray();
            if (requiredEvents.All(expected => added.Any(line =>
                    line.Contains(expected, StringComparison.Ordinal)))) return snapshot;

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero
                || await Task.WhenAny(pulseTask, Task.Delay(remaining, cancellationToken)) != pulseTask)
                throw new TimeoutException("Expected connection evidence did not arrive.");
        }
    }


    public string[] Snapshot() => _entries.ToArray();
}

// Polls the mesh runtime snapshot and turns peer ready transitions into the
// connection-evidence lines the scenarios wait on. Wire-level socket sources
// do not exist for mesh nodes (spec 50 owns the mesh monitoring surface).
internal sealed class MeshConnectionObserverService(
    IServiceProvider services,
    ConnectionEvidence evidence,
    string linePrefix) : BackgroundService
{
    private int _debugTick;

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine("[mesh-observer] started");
        var meshRuntime = services.GetRequiredService<IZLinkRouteMeshRuntime>();
        var query = services.GetRequiredService<IZLinkLocationRuntimeQuery>();
        var ready = new Dictionary<string, string>(StringComparer.Ordinal);
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                var snapshot = meshRuntime.Snapshot(ResilienceLifecycleNames.Channel);
                // Core reuses the peer entry (and its dialed-endpoint label)
                // across same-RID lifetime replacements; the descriptor row
                // carries the endpoint the peer currently advertises.
                var rows = await query.ListMeshNodeDescriptorsAsync(
                    ResilienceLifecycleNames.Channel, stoppingToken);
                var advertised = rows.ToDictionary(
                    static row => row.Rid.ToString(),
                    static row => row.Endpoint,
                    StringComparer.Ordinal);
                if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1"
                    && Interlocked.Increment(ref _debugTick) % 20 == 1)
                    Console.Error.WriteLine(
                        $"[mesh-observer] state={snapshot.State} peers=[{string.Join(",", snapshot.Peers.Select(p => $"{p.Rid}:{p.AdmissionState}@{p.Endpoint}"))}]");
                var current = snapshot.Peers
                    .Where(static peer => peer.Ready)
                    .ToDictionary(
                        static peer => peer.Rid.ToString(),
                        peer => advertised.TryGetValue(peer.Rid.ToString(), out var endpoint)
                            ? endpoint
                            : peer.Endpoint,
                        StringComparer.Ordinal);
                foreach (var (rid, endpoint) in current)
                    if (!ready.ContainsKey(rid))
                    {
                        evidence.Add($"{linePrefix}kind=ConnectionReady|remote={endpoint}|routing={rid}");
                        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                            Console.Error.WriteLine($"[mesh-observer] ready rid={rid} ep={endpoint}");
                    }
                foreach (var (rid, endpoint) in ready)
                    if (!current.ContainsKey(rid))
                    {
                        evidence.Add($"{linePrefix}kind=Disconnected|remote={endpoint}|routing={rid}");
                        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                            Console.Error.WriteLine($"[mesh-observer] gone rid={rid} ep={endpoint}");
                    }
                ready = current;
            }
            catch (Exception error)
            {
                // The mesh node may not be started yet; keep polling.
                if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                    Console.Error.WriteLine($"[mesh-observer] error {error.GetType().Name}: {error.Message}");
            }

            try
            {
                await Task.Delay(TimeSpan.FromMilliseconds(100), stoppingToken);
            }
            catch (OperationCanceledException)
            {
                return;
            }
        }
    }
}
