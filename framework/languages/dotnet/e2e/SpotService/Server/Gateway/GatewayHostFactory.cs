using Microsoft.Extensions.Configuration;

using System.Collections.Concurrent;
using System.Diagnostics;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

using Zlink.Framework.Locations.Redis;

namespace SpotService.Server.Gateway;

using Zlink.Framework.E2E.Configuration;

internal static class GatewayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = GatewayOptions.Parse(args);
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
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix(options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."))));
                // Crash-recovery scenarios re-claim actors from a killed
                // node; a short owner lease keeps that takeover window
                // within the scenario's patience.
                var locations = framework.ConfigureLocations();
                locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
                locations.OwnerLeaseTtl = TimeSpan.FromSeconds(10);
                locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            }
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            var externalMesh = framework.AddRouteMesh(SpotServiceNames.ExternalSpotChannel)
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From(options.Rid));
            externalMesh.ChannelName(SpotServiceNames.ExternalSpotChannel).SetWeight(0);
            externalMesh.PeerConnections.Connect(
                Require(options.ExternalSpotEndpoint, "ExternalSpotEndpoint"));
            var mesh19 = framework.AddRouteMesh(SpotServiceNames.SpotChannel)
                .Listen(Require(options.SpotRouterEndpoint, "SpotRouterEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            mesh19.ChannelName(SpotServiceNames.SpotChannel);
            var publisherConfig = mesh19.ConfigureSpotPublisher();
            publisherConfig.SendHighWaterMark = options.SpotPublisherSendHighWaterMark;
            publisherConfig.SendTimeout = TimeSpan.FromMilliseconds(
                options.SpotPublisherSendTimeoutMilliseconds);
            mesh19.ConfigureRouterSocket().SendTimeout = TimeSpan.FromMilliseconds(
                options.SpotPublisherSendTimeoutMilliseconds);
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
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
        app.MapPost("/spot/publish", async (
            SpotPublishReq request,
            IZLinkSpotPublisherClient publisher,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            await publisher.Publish(
                    SpotServiceNames.SpotChannel,
                    SpotServiceNames.SpotChannel,
                    SpotServiceNames.SpotMsgTopic,
                    new SpotMsg(request.Marker))
                .Async(cancellationToken);
            evidence.Add($"spot-publish|rid={options.Rid}|spot={request.SpotRid}|marker={request.Marker}");
            return Results.Ok(new SpotPublishRes(
                "spot.sm-c4-publish",
                options.Rid,
                request.SpotRid,
                request.Marker,
                evidence.Snapshot()));
        });
        app.MapPost("/spot/backpressure-publish", async (
            SpotBackpressurePublishReq request,
            IZLinkSpotPublisherClient publisher,
            CancellationToken cancellationToken) =>
        {
            var payload = new string('x', Math.Clamp(request.PayloadBytes, 1024, 1024 * 1024));
            var maxAttempts = Math.Clamp(request.MaxAttempts, 1, 20000);
            var startSequence = Math.Max(1, request.StartSequence);
            var observed = new Dictionary<string, int>(StringComparer.Ordinal);

            for (var offset = 0; offset < maxAttempts; offset++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var sequence = startSequence + offset;
                var started = System.Diagnostics.Stopwatch.GetTimestamp();
                var call = publisher.Publish(
                    SpotServiceNames.SpotChannel,
                    SpotServiceNames.SpotChannel,
                    SpotServiceNames.SpotMsgTopic,
                    new SpotBackpressureMsg(request.Marker, sequence, payload));
                var result = await call.Async(cancellationToken);
                var elapsed = System.Diagnostics.Stopwatch.GetElapsedTime(started);
                var observation =
                    $"status={result.Status},snapshot={result.Detail.SnapshotRemoteNodeCount}"
                    + $",admitted={result.Detail.AdmittedRemoteNodeCount}"
                    + $",dropped={result.Detail.DroppedRemoteNodeCount}";
                observed[observation] = observed.GetValueOrDefault(observation) + 1;
                if (result.Detail.SnapshotRemoteNodeCount >= 2
                    && result.Detail.AdmittedRemoteNodeCount + 1
                        == result.Detail.SnapshotRemoteNodeCount
                    && result.Detail.DroppedRemoteNodeCount == 1)
                    return Results.Ok(ToAttempt(sequence, result, elapsed));
                if (request.Blocking)
                {
                    Console.WriteLine(
                        $"spot-service sm-c6 blocking-attempt sequence={sequence}"
                        + $" status={result.Status}"
                        + $" elapsed_ms={(long)elapsed.TotalMilliseconds}"
                        + $" snapshot={result.Detail.SnapshotRemoteNodeCount}"
                        + $" admitted={result.Detail.AdmittedRemoteNodeCount}"
                        + $" dropped={result.Detail.DroppedRemoteNodeCount}");
                }
                if (!request.Blocking)
                    await Task.Delay(TimeSpan.FromMilliseconds(5), cancellationToken);
            }

            var observations = string.Join(
                "; ",
                observed.OrderBy(entry => entry.Key, StringComparer.Ordinal)
                    .Select(entry => $"{entry.Key},count={entry.Value}"));
            Console.WriteLine(
                $"spot-service sm-c6 {(request.Blocking ? "blocking" : "non-blocking")} exhausted"
                + $" attempts={maxAttempts} observations=[{observations}]");
            return Results.Problem(
                $"{(request.Blocking ? "Blocking" : "Non-blocking")} ROUTER backpressure"
                + $" did not produce one admitted and one dropped target in {maxAttempts} publishes."
                + $" Observed: {observations}",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/channel/route-ping", async (
            IZLinkRouteClient routes,
            ControlPingReq request,
            CancellationToken cancellationToken) =>
        {
            var reply = await routes.RequestToNode(
                    SpotServiceNames.ExternalSpotChannel,
                    RoutingId.From("play-a"),
                    request)
                .Async<ControlPingRes>(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/spot/route-state", async (
            IZLinkSpotClient spotsClient,
            IZLinkSpotHandleResolver handles,
            SpotStateRouteReq request,
            CancellationToken cancellationToken) =>
        {
            var target = await handles.ResolveRequiredAsync(request.SpotRid, cancellationToken);
            var reply = await spotsClient.RequestToSpot(target, new StateReq(request.Operation, request.Delta))
                .Async<StateRes>(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/actor/push", async (
            ActorPushByActorReq request,
            IZLinkActorManager actorDirectory,
            IZLinkActorClient actors,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            evidence.Add($"actor-push-request|rid={options.Rid}|actor={request.ActorId}|value={request.Value}");
            try
            {
                var actor = await actorDirectory.FindAsync(request.ActorId, cancellationToken)
                            ?? throw new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                $"Actor route '{request.ActorId}' was not found.");
                var reply = await actors.RequestToActor(
                        SpotServiceNames.SpotChannel,
                        actor,
                        new ActorPushReq(request.Value))
                    .Timeout(TimeSpan.FromSeconds(10))
                    .Async<ActorPingRes>(cancellationToken);
                evidence.Add(
                    $"actor-push-delivered|rid={options.Rid}|actor={reply.ActorId}"
                    + $"|value={reply.Value}|node={reply.NodeRid}");
                return Results.Ok(new ActorPushByActorRes(reply.ActorId, reply.Value, true, string.Empty));
            }
            catch (ZLinkFrameworkException ex)
            {
                evidence.Add(
                    $"actor-push-failed|rid={options.Rid}|actor={request.ActorId}"
                    + $"|error={ex.GetType().Name}|kind={ex.Kind}");
                return Results.Ok(new ActorPushByActorRes(
                    request.ActorId,
                    request.Value,
                    false,
                    ex.Kind.ToString()));
            }
            catch (Exception ex)
            {
                evidence.Add(
                    $"actor-push-failed|rid={options.Rid}|actor={request.ActorId}"
                    + $"|error={ex.GetType().Name}");
                return Results.Ok(new ActorPushByActorRes(
                    request.ActorId,
                    request.Value,
                    false,
                    ex.GetType().Name));
            }
        });
        app.MapPost("/actor/wait-missing", async (
            ActorMissingWaitReq request,
            IZLinkActorManager actorDirectory,
            CancellationToken cancellationToken) =>
        {
            var deadline = DateTimeOffset.UtcNow
                           + TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            while (DateTimeOffset.UtcNow < deadline)
            {
                if (await actorDirectory.FindAsync(request.ActorId, cancellationToken) is null)
                    return Results.Ok();
                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            }

            return Results.Problem(
                $"Actor route '{request.ActorId}' remained visible.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/actor/capture-ref", async (
            ActorRefSnapshotReq request,
            IZLinkActorManager actors,
            CancellationToken cancellationToken) =>
        {
            var actor = await actors.FindAsync(request.ActorId, cancellationToken)
                        ?? throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorRouteNotFound,
                            $"Actor route '{request.ActorId}' was not found.");
            return Results.Ok(new ActorRefSnapshotRes(
                actor.ActorId, actor.NodeRid.ToString(), actor.Generation));
        });
        app.MapPost("/actor/request-ref", async (
            ActorRefRequestReq request,
            IZLinkActorClient actors,
            CancellationToken cancellationToken) =>
        {
            try
            {
                var actor = new ActorRef(
                    RoutingId.From(request.Actor.NodeRid),
                    request.Actor.ActorId,
                    request.Actor.Generation);
                var call = request.DelayMilliseconds > 0
                    ? actors.RequestToActor(SpotServiceNames.SpotChannel, actor,
                        new SlowActorPingReq(request.Value, request.DelayMilliseconds))
                    : actors.RequestToActor(
                        SpotServiceNames.SpotChannel, actor, new ActorPingReq(request.Value));
                var reply = await call
                    .Timeout(TimeSpan.FromMilliseconds(
                        Math.Clamp(request.TimeoutMilliseconds, 1, 30000)))
                    .Async<ActorPingRes>(cancellationToken);
                return Results.Ok(new ActorRefRequestRes(true, string.Empty, reply));
            }
            catch (ZLinkFrameworkException error)
            {
                return Results.Ok(new ActorRefRequestRes(false, error.Kind.ToString(), null));
            }
            catch (TimeoutException)
            {
                return Results.Ok(new ActorRefRequestRes(false, "Timeout", null));
            }
        });
        app.MapPost("/node/wait-ready", async (
            NodeReadinessWaitReq request,
            IZLinkLocationRuntimeQuery locations,
            IZLinkSpotHandleResolver handles,
            IZLinkSpotClient spotsClient,
            IZLinkRouteMeshRuntime meshRuntime,
            CancellationToken cancellationToken) =>
        {
            var deadline = DateTimeOffset.UtcNow
                           + TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var lastPeerReady = false;
            var lastEntryReady = false;
            string? lastProbeError = null;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var peers = await locations.ListMeshNodeDescriptorsAsync(
                    SpotServiceNames.SpotChannel, cancellationToken);
                var peerReady = peers.Any(row => string.Equals(
                    row.Rid.ToString(), request.NodeRid, StringComparison.Ordinal));
                var entry = await handles.ResolveSpotHandleAsync(
                    SpotServiceNames.SpotChannel,
                    RoutingId.From(request.NodeRid),
                    cancellationToken);
                lastPeerReady = peerReady;
                lastEntryReady = entry is not null;
                if (peerReady && entry is not null)
                    try
                    {
                        var marker = $"ready-{Guid.NewGuid():N}";
                        var reply = await spotsClient.RequestToSpot(entry, new EntryReadinessReq(marker))
                            .Timeout(TimeSpan.FromSeconds(1))
                            .Async<EntryReadinessRes>(cancellationToken);
                        if (reply.NodeRid == request.NodeRid && reply.Marker == marker)
                            return Results.Ok(new NodeReadinessWaitRes(request.NodeRid, true, true));
                    }
                    catch (Exception error) when (!cancellationToken.IsCancellationRequested)
                    {
                        // The location row can precede the router connection.
                        lastProbeError = $"{error.GetType().Name}: {error.Message}";
                    }
                await Task.Delay(100, cancellationToken);
            }
            Console.WriteLine(
                $"spot-service node-readiness-timeout node={request.NodeRid}"
                + $" peer={lastPeerReady} entry={lastEntryReady}"
                + $" probe={lastProbeError ?? "not-attempted"}"
                + $" mesh-peers={string.Join(',', meshRuntime.Snapshot(SpotServiceNames.SpotChannel).Peers
                    .Select(peer => $"{peer.Rid}:{peer.AdmissionState}:{peer.LastFailure ?? "none"}"))}");
            return Results.Problem(
                $"Node '{request.NodeRid}' peer and Entry Spot readiness did not converge.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/entry/join", async (
            EntryJoinRouteReq request,
            IZLinkSpotClient spotsClient,
            IZLinkSpotHandleResolver handles,
            IZLinkActorManager actors,
            CancellationToken cancellationToken) =>
        {
            var entry = await handles.ResolveSpotHandleAsync(
                            SpotServiceNames.SpotChannel,
                            RoutingId.From(request.NodeRid),
                            cancellationToken)
                        ?? throw new InvalidOperationException(
                            $"Entry Spot for node '{request.NodeRid}' is not ready.");
            var joined = await spotsClient.RequestToSpot(entry, request.Join)
                .Async<JoinRes>(cancellationToken);
            var elapsed = Stopwatch.StartNew();
            while (elapsed.Elapsed < TimeSpan.FromSeconds(3))
            {
                var actor = await actors.FindAsync(request.Join.ActorId, cancellationToken);
                if (actor is not null) return Results.Ok(joined);
                await Task.Delay(100, cancellationToken);
            }
            return Results.Problem(
                $"Actor '{request.Join.ActorId}' did not become ready after Entry Spot JoinReq.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }

    static SpotBackpressureAttemptRes ToAttempt(
        int sequence,
        ZLinkPublishResult result,
        TimeSpan elapsed)
        => new(
            sequence,
            result.Status.ToString(),
            (long)elapsed.TotalMilliseconds,
            result.Detail.SnapshotRemoteNodeCount,
            result.Detail.AdmittedRemoteNodeCount,
            result.Detail.DroppedRemoteNodeCount,
            result.Detail.SnapshotLocalSpotCount,
            result.Detail.AdmittedLocalSpotCount,
            result.Detail.DroppedLocalSpotCount);

    static string Require(string? value, string optionName)
        => string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{optionName} is required.")
            : value;
}

internal sealed class EvidenceStore
{
    readonly ConcurrentQueue<string> _entries = new();
    readonly string? _file;
    // A pulse completed on every Add and swapped for a fresh one, so EVERY
    // concurrent waiter wakes — a counted semaphore hands one release to one
    // waiter and silently starves the rest.
    readonly object _pulseGate = new();
    TaskCompletionSource<bool> _pulse =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public EvidenceStore(string rid, string? file)
    {
        _file = file;
        Add($"start|rid={rid}");
    }

    public void Add(string value)
    {
        _entries.Enqueue(value);
        TaskCompletionSource<bool> pulse;
        lock (_pulseGate)
        {
            pulse = _pulse;
            _pulse = new TaskCompletionSource<bool>(
                TaskCreationOptions.RunContinuationsAsynchronously);
        }

        pulse.TrySetResult(true);
        if (!string.IsNullOrWhiteSpace(_file))
        {
            File.AppendAllLines(_file, new[] { value });
        }
    }

    public string[] Snapshot() => _entries.ToArray();

    public async Task<string[]> WaitUntilAsync(
        Func<string[], bool> condition,
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

            var snapshot = Snapshot();
            if (condition(snapshot)) return snapshot;

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero) throw new TimeoutException("Timed out waiting for spot service evidence.");

            await Task.WhenAny(pulseTask, Task.Delay(remaining, cancellationToken));
        }
    }
}

internal sealed record GatewayOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile = null,
    string? RedisEndpoint = null,
    string? RedisKeyPrefix = null,
    string? SpotRouterEndpoint = null,
    string? SpotPubEndpoint = null,
    string? ExternalSpotEndpoint = null,
    string? SpotPeerAEndpoint = null,
    string? SpotPeerBEndpoint = null,
    int SpotPublisherSendHighWaterMark = 1,
    int SpotPublisherSendTimeoutMilliseconds = 250)
{
    public static GatewayOptions Parse(string[] args)
        => E2eConfiguration.Load<GatewayOptions>(args);
}
