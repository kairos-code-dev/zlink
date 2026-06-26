using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;

namespace YieldDispatch.Server.Play;

internal static class PlayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = PlayOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new NodeOptions(options.Rid));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
            framework.AddRouteMesh(YieldDispatchNames.ControlChannel)
                .EnableServer(options.ControlEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddRequestHandler<RunTrackAControlHandler, YieldTrackAReq, YieldScenarioResult>("YieldTrackAReq")
                .AddRequestHandler<RunTimeoutControlHandler, YieldTimeoutScenarioReq, YieldScenarioResult>("YieldTimeoutScenarioReq")
                .AddRequestHandler<RunTimerControlHandler, YieldTimerScenarioReq, YieldScenarioResult>("YieldTimerScenarioReq")
                .AddRequestHandler<RunRemoteControlHandler, YieldRemoteScenarioReq, YieldScenarioResult>("YieldRemoteScenarioReq")
                .AddRequestHandler<RunRouteBridgeControlHandler, YieldRouteBridgeScenarioReq, YieldScenarioResult>("YieldRouteBridgeScenarioReq")
                .AddRequestHandler<RunCancellationControlHandler, YieldCancellationScenarioReq, YieldScenarioResult>("YieldCancellationScenarioReq")
                .AddRequestHandler<BindYieldActorsControlHandler, BindYieldActorsReq, BindYieldActorsReply>("BindYieldActorsReq")
                .AddRequestHandler<EnsureSpotControlHandler, EnsureSpotReq, EnsureSpotReply>("EnsureSpotReq")
                .AddRequestHandler<YieldEvidenceControlHandler, YieldEvidenceReq, YieldEvidenceReply>("YieldEvidenceReq");
            framework.AddClientServerChannel(YieldDispatchNames.DelayChannel)
                .EnableClient(options.DelayEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddRouteMesh(YieldDispatchNames.SpotRouteChannel)
                .EnableServer(options.SpotRouteEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddSpotMesh(YieldDispatchNames.SpotChannel)
                .UseRegistrySpotResolver()
                .EnableRouter(options.SpotRouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .EnablePubSub(options.SpotPubEndpoint)
                .AddEntrySpot<YieldEntrySpot>()
                .AddActorFactory<YieldActorFactory>(YieldDispatchNames.ActorType)
                .AddSpotFactory<YieldProbeSpot>();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "play", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }

    internal static async Task<YieldScenarioResult> RunTrackAAsync(
        IZLinkSpotManager spots,
        IZLinkRouteClient routes,
        EvidenceStore evidence,
        NodeOptions node)
    {
        var spotRid = $"yield-track-a-{Guid.NewGuid():N}";
        await spots.GetOrCreateAsync<YieldProbeSpot>(RoutingId.From(spotRid));
        var before = evidence.Snapshot();

        var holdId = $"YD-A1-{Guid.NewGuid():N}";
        var holdTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new HoldReq(holdId, 350),
            "HoldReq");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"hold-started|rid={node.Rid}|spot={spotRid}|request={holdId}") == 1,
            "YD-A1 hold did not start.");
        var holdProbeTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new ProbeReq(holdId, "hold-probe"),
            "ProbeReq");
        await Task.WhenAll(holdTask, holdProbeTask);

        var yieldId = $"YD-A2-{Guid.NewGuid():N}";
        var yieldTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new YieldReq(yieldId, 350, "corr-a2"),
            "YieldReq");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"yield-released|rid={node.Rid}|spot={spotRid}|request={yieldId}") == 1,
            "YD-A2 yield did not release.");
        var yieldProbeTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new ProbeReq(yieldId, "yield-probe"),
            "ProbeReq");
        await Task.WhenAll(yieldTask, yieldProbeTask);

        var contextId = $"YD-A3-{Guid.NewGuid():N}";
        var contextReply = await RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new YieldReq(contextId, 50, "corr-a3"),
            "YieldReq");
        Ensure(contextReply.RequestId == contextId, "YD-A3 request id was not preserved.");
        Ensure(contextReply.SpotRid == spotRid, "YD-A3 spot rid was not preserved.");

        var workerId = $"YD-A4-{Guid.NewGuid():N}";
        var workerTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new WorkerYieldReq(workerId, 350),
            "WorkerYieldReq");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"worker-yield-released|rid={node.Rid}|spot={spotRid}|request={workerId}") == 1,
            "YD-A4 worker yield did not release.");
        var workerProbeTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new ProbeReq(workerId, "worker-probe"),
            "ProbeReq");
        await Task.WhenAll(workerTask, workerProbeTask);

        var after = evidence.Snapshot();
        AssertOrder(after, $"request={holdId}", [
            "hold-started",
            "hold-resumed",
            "hold-completed",
            "probe-started"]);
        AssertOrder(after, $"request={yieldId}", [
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"]);
        AssertOrder(after, $"request={workerId}", [
            "worker-yield-started",
            "worker-yield-released",
            "probe-started",
            "probe-completed",
            "worker-yield-resumed",
            "worker-yield-completed"]);

        return new YieldScenarioResult("yield.track-a", spotRid, after);
    }

    internal static async Task<YieldScenarioResult> RunTimeoutAsync(
        IZLinkSpotManager spots,
        IZLinkRouteClient routes,
        EvidenceStore evidence,
        NodeOptions node)
    {
        var spotRid = $"yield-timeout-{Guid.NewGuid():N}";
        await spots.GetOrCreateAsync<YieldProbeSpot>(RoutingId.From(spotRid));
        var before = evidence.Snapshot();
        var requestId = $"YD-E1-{Guid.NewGuid():N}";

        var reply = await RequestSpotAsync<YieldTimeoutReply>(
            routes,
            spotRid,
            new YieldTimeoutReq(requestId, DelayMs: 700, TimeoutMs: 100),
            "YieldTimeoutReq");
        Ensure(reply.TimedOut, "YD-E1 expected timeout reply.");

        var probe = await RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new ProbeReq(requestId, "timeout-probe"),
            "ProbeReq");
        Ensure(probe.RequestId == requestId, "YD-E1 post-timeout probe request mismatch.");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"probe-completed|rid={node.Rid}|spot={spotRid}|request={requestId}|marker=timeout-probe") == 1,
            "YD-E1 mailbox did not accept work after timeout.");

        return new YieldScenarioResult("yield.e1-timeout", spotRid, evidence.Snapshot());
    }

    internal static async Task<YieldScenarioResult> RunRemoteAsync(
        IZLinkSpotManager spots,
        IZLinkRouteClient routes,
        EvidenceStore evidence,
        NodeOptions node,
        string targetNodeRid)
    {
        var ownerSpotRid = $"yield-remote-owner-{Guid.NewGuid():N}";
        var targetSpotRid = $"yield-remote-target-{Guid.NewGuid():N}";
        await spots.GetOrCreateAsync<YieldProbeSpot>(RoutingId.From(ownerSpotRid));
        await RequestControlAsync<EnsureSpotReply>(
            routes,
            RoutingId.From(targetNodeRid),
            new EnsureSpotReq(targetSpotRid),
            "EnsureSpotReq");
        var before = evidence.Snapshot();
        var requestId = $"YD-D2-{Guid.NewGuid():N}";

        var remoteTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            ownerSpotRid,
            new RemoteSpotYieldReq(requestId, targetSpotRid, 350),
            "RemoteSpotYieldReq");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"remote-yield-released|rid={node.Rid}|spot={ownerSpotRid}|request={requestId}") == 1,
            "YD-D2 remote yield did not release.");
        var probeTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            ownerSpotRid,
            new ProbeReq(requestId, "remote-owner-probe"),
            "ProbeReq");
        await Task.WhenAll(remoteTask, probeTask);
        Ensure(remoteTask.Result.NodeRid == node.Rid, "YD-D2 continuation did not return to the owner node.");

        var targetEvidence = await RequestControlAsync<YieldEvidenceReply>(
            routes,
            RoutingId.From(targetNodeRid),
            new YieldEvidenceReq(requestId),
            "YieldEvidenceReq");
        Ensure(
            targetEvidence.Evidence.Any(line => line.Contains($"yield-started|rid={targetNodeRid}|spot={targetSpotRid}|request={requestId}", StringComparison.Ordinal)),
            "YD-D2 target node did not process the remote Spot request.");
        Ensure(
            targetEvidence.Evidence.All(line => !line.Contains($"remote-yield-resumed|rid={targetNodeRid}", StringComparison.Ordinal)),
            "YD-D2 target node must not own the caller continuation.");
        AssertOrder(evidence.Snapshot(), $"request={requestId}", [
            "remote-yield-started",
            "remote-yield-released",
            "probe-started",
            "probe-completed",
            "remote-yield-resumed",
            "remote-yield-completed"]);

        return new YieldScenarioResult("yield.d2-remote", ownerSpotRid, evidence.Snapshot().Concat(targetEvidence.Evidence).ToArray());
    }

    internal static async Task<YieldScenarioResult> RunRouteBridgeAsync(
        IZLinkSpotManager spots,
        IZLinkRouteClient routes,
        EvidenceStore evidence,
        NodeOptions node,
        string requestId)
    {
        var spotRid = $"yield-route-bridge-{Guid.NewGuid():N}";
        await spots.GetOrCreateAsync<YieldProbeSpot>(RoutingId.From(spotRid));
        var before = evidence.Snapshot();

        var yieldTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new YieldReq(requestId, 250, "route-bridge"),
            "YieldReq");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"yield-released|rid={node.Rid}|spot={spotRid}|request={requestId}") == 1,
            "YD-D3 route bridge yield did not release.");
        var probeTask = RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new ProbeReq(requestId, "route-bridge-probe"),
            "ProbeReq");
        await Task.WhenAll(yieldTask, probeTask);
        var after = evidence.Snapshot();
        Ensure(
            after.Any(line => line.Contains($"yield-started|rid={node.Rid}|spot={spotRid}|request={requestId}", StringComparison.Ordinal)),
            "YD-D3 target Spot handler marker missing.");
        AssertOrder(after, $"request={requestId}", [
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"]);

        return new YieldScenarioResult("yield.d3-route-bridge", spotRid, after);
    }

    internal static async Task<YieldScenarioResult> RunCancellationAsync(
        IZLinkSpotManager spots,
        IZLinkRouteClient routes,
        EvidenceStore evidence,
        NodeOptions node)
    {
        var spotRid = $"yield-cancel-{Guid.NewGuid():N}";
        await spots.GetOrCreateAsync<YieldProbeSpot>(RoutingId.From(spotRid));
        var before = evidence.Snapshot();
        var requestId = $"YD-E2-{Guid.NewGuid():N}";

        var reply = await RequestSpotAsync<YieldCancelReply>(
            routes,
            spotRid,
            new YieldCancelReq(requestId, DelayMs: 800, CancelAfterMs: 100),
            "YieldCancelReq");
        Ensure(reply.Canceled, "YD-E2 expected cancellation reply.");

        var probe = await RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new ProbeReq(requestId, "cancel-probe"),
            "ProbeReq");
        Ensure(probe.RequestId == requestId, "YD-E2 post-cancel probe request mismatch.");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"probe-completed|rid={node.Rid}|spot={spotRid}|request={requestId}|marker=cancel-probe") == 1,
            "YD-E2 mailbox did not accept work after cancellation.");

        return new YieldScenarioResult("yield.e2-cancellation", spotRid, evidence.Snapshot());
    }

    internal static async Task<YieldScenarioResult> RunTimerAsync(
        IZLinkSpotManager spots,
        IZLinkRouteClient routes,
        EvidenceStore evidence,
        NodeOptions node)
    {
        var spotRid = $"yield-timer-{Guid.NewGuid():N}";
        await spots.GetOrCreateAsync<YieldProbeSpot>(RoutingId.From(spotRid));
        var before = evidence.Snapshot();

        var c1 = $"YD-C1-{Guid.NewGuid():N}";
        await RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new TimerStartReq(c1, $"{c1}-yield", "yield-on-first", 50, 350),
            "TimerStartReq");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"timer-yield-released|rid={node.Rid}|spot={spotRid}|request={c1}") == 1,
            "YD-C1 timer yield did not release.");
        await RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new TimerStartReq(c1, $"{c1}-fast", "fast", 50, 0),
            "TimerStartReq");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"timer-yield-completed|rid={node.Rid}|spot={spotRid}|request={c1}") == 1
                && CountNew(evidence.Snapshot(), before, $"timer-fast-completed|rid={node.Rid}|spot={spotRid}|request={c1}") >= 1,
            "YD-C1 timer markers were not observed.");
        AssertOrder(evidence.Snapshot(), c1, [
            "timer-yield-started",
            "timer-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"]);
        await RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new TimerStopReq(c1),
            "TimerStopReq");

        var c2 = $"YD-C2-{Guid.NewGuid():N}";
        await RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new TimerStartReq(c2, $"{c2}-same", "yield-then-next", 50, 350),
            "TimerStartReq");
        await WaitUntilAsync(
            () => CountNew(evidence.Snapshot(), before, $"timer-next-completed|rid={node.Rid}|spot={spotRid}|request={c2}") == 1,
            "YD-C2 same timer did not deliver the next tick after yield completion.");
        AssertOrder(evidence.Snapshot(), c2, [
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed",
            "timer-next-started",
            "timer-next-completed"]);
        await RequestSpotAsync<YieldDispatchReply>(
            routes,
            spotRid,
            new TimerStopReq(c2),
            "TimerStopReq");

        return new YieldScenarioResult("yield.track-c-timer", spotRid, evidence.Snapshot());
    }

    private static async Task<TReply> RequestSpotAsync<TReply>(
        IZLinkRouteClient routes,
        string spotRid,
        object request,
        string packetName)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await routes.Request(
                        YieldDispatchNames.SpotRouteChannel,
                        RoutingId.From(spotRid),
                        request)
                    .PacketName(packetName)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<TReply>();
            }
            catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
            {
                last = ex;
                await Task.Delay(100);
            }
        }

        throw new TimeoutException($"Timed out requesting spot '{spotRid}' packet '{packetName}'.", last);
    }

    private static async Task<TReply> RequestControlAsync<TReply>(
        IZLinkRouteClient routes,
        RoutingId target,
        object request,
        string packetName)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await routes.Request(
                        YieldDispatchNames.ControlChannel,
                        target,
                        request)
                    .PacketName(packetName)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<TReply>();
            }
            catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
            {
                last = ex;
                await Task.Delay(100);
            }
        }

        throw new TimeoutException($"Timed out requesting control '{target}' packet '{packetName}'.", last);
    }

    private static async Task WaitUntilAsync(Func<bool> condition, string failureMessage)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (condition())
            {
                return;
            }

            await Task.Delay(25);
        }

        throw new TimeoutException(failureMessage);
    }

    private static int CountNew(string[] after, string[] before, string pattern)
    {
        var beforeCount = before.Count(line => line.Contains(pattern, StringComparison.Ordinal));
        var afterCount = after.Count(line => line.Contains(pattern, StringComparison.Ordinal));
        return afterCount - beforeCount;
    }

    private static void AssertOrder(string[] evidence, string requestFilter, string[] markers)
    {
        var cursor = -1;
        foreach (var marker in markers)
        {
            var index = Array.FindIndex(
                evidence,
                cursor + 1,
                line => line.Contains(requestFilter, StringComparison.Ordinal)
                    && line.Contains(marker, StringComparison.Ordinal));
            if (index < 0)
            {
                throw new InvalidOperationException($"Missing ordered marker '{marker}' for {requestFilter}.");
            }

            cursor = index;
        }
    }

    private static void Ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}

internal sealed class YieldProbeSpot(
    IZLinkSpotContext context,
    EvidenceStore evidence) : IZLinkSpot<YieldActor>
{
    private readonly object _timerGate = new();
    private readonly Dictionary<string, YieldTimerState> _timers = new(StringComparer.Ordinal);

    public IZLinkSpotContext Context { get; } = context;

    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        YieldActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!request.IsEmpty)
        {
            var delay = request.Decode<DelayReq>();
            if (delay.DelayMs > 0)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(delay.DelayMs), cancellationToken);
            }
        }

        evidence.Add($"actor-joined|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actor.ActorId}");
        return ZLinkSpotActorJoinResult.Accept(request);
    }

    public void AddTimerState(YieldTimerState state)
    {
        lock (_timerGate)
        {
            _timers[state.TimerName] = state;
        }
    }

    public YieldTimerState? FindTimerState(string timerName)
    {
        lock (_timerGate)
        {
            return _timers.TryGetValue(timerName, out var state) ? state : null;
        }
    }

    public async ValueTask StopScenarioTimersAsync(string requestId)
    {
        List<YieldTimerState> states;
        lock (_timerGate)
        {
            states = _timers.Values
                .Where(state => string.Equals(state.RequestId, requestId, StringComparison.Ordinal))
                .ToList();
            foreach (var state in states)
            {
                _timers.Remove(state.TimerName);
            }
        }

        foreach (var state in states)
        {
            if (state.Timer is not null)
            {
                await state.Timer.CancelAsync();
            }
        }
    }
}

internal sealed class YieldTimerState(
    string requestId,
    string timerName,
    string mode,
    int delayMs)
{
    private int _tickCount;

    public string RequestId { get; } = requestId;

    public string TimerName { get; } = timerName;

    public string Mode { get; } = mode;

    public int DelayMs { get; } = delayMs;

    public IZLinkTimer? Timer { get; set; }

    public int NextTick() => Interlocked.Increment(ref _tickCount);
}

internal sealed class RunTrackAControlHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    EvidenceStore evidence,
    NodeOptions node)
    : IZLinkRouteRequestHandler<YieldTrackAReq, YieldScenarioResult>
{
    public ValueTask<YieldScenarioResult> HandleAsync(
        YieldTrackAReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = request;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return new ValueTask<YieldScenarioResult>(
            PlayHostFactory.RunTrackAAsync(spots, routes, evidence, node));
    }
}

internal sealed class RunTimeoutControlHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    EvidenceStore evidence,
    NodeOptions node)
    : IZLinkRouteRequestHandler<YieldTimeoutScenarioReq, YieldScenarioResult>
{
    public ValueTask<YieldScenarioResult> HandleAsync(
        YieldTimeoutScenarioReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = request;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return new ValueTask<YieldScenarioResult>(
            PlayHostFactory.RunTimeoutAsync(spots, routes, evidence, node));
    }
}

internal sealed class RunTimerControlHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    EvidenceStore evidence,
    NodeOptions node)
    : IZLinkRouteRequestHandler<YieldTimerScenarioReq, YieldScenarioResult>
{
    public ValueTask<YieldScenarioResult> HandleAsync(
        YieldTimerScenarioReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = request;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return new ValueTask<YieldScenarioResult>(
            PlayHostFactory.RunTimerAsync(spots, routes, evidence, node));
    }
}

internal sealed class RunRemoteControlHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    EvidenceStore evidence,
    NodeOptions node)
    : IZLinkRouteRequestHandler<YieldRemoteScenarioReq, YieldScenarioResult>
{
    public ValueTask<YieldScenarioResult> HandleAsync(
        YieldRemoteScenarioReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return new ValueTask<YieldScenarioResult>(
            PlayHostFactory.RunRemoteAsync(spots, routes, evidence, node, request.TargetNodeRid));
    }
}

internal sealed class RunRouteBridgeControlHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    EvidenceStore evidence,
    NodeOptions node)
    : IZLinkRouteRequestHandler<YieldRouteBridgeScenarioReq, YieldScenarioResult>
{
    public ValueTask<YieldScenarioResult> HandleAsync(
        YieldRouteBridgeScenarioReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return new ValueTask<YieldScenarioResult>(
            PlayHostFactory.RunRouteBridgeAsync(spots, routes, evidence, node, request.RequestId));
    }
}

internal sealed class RunCancellationControlHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    EvidenceStore evidence,
    NodeOptions node)
    : IZLinkRouteRequestHandler<YieldCancellationScenarioReq, YieldScenarioResult>
{
    public ValueTask<YieldScenarioResult> HandleAsync(
        YieldCancellationScenarioReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = request;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return new ValueTask<YieldScenarioResult>(
            PlayHostFactory.RunCancellationAsync(spots, routes, evidence, node));
    }
}

internal sealed class BindYieldActorsControlHandler(
    IZLinkActorManager actors,
    IZLinkSpotManager spots,
    EvidenceStore evidence,
    NodeOptions node)
    : IZLinkRouteRequestHandler<BindYieldActorsReq, BindYieldActorsReply>
{
    public async ValueTask<BindYieldActorsReply> HandleAsync(
        BindYieldActorsReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await spots.GetOrCreateAsync<YieldProbeSpot>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        var bindings = new List<YieldActorBinding>();
        foreach (var actorId in request.ActorIds)
        {
            var actor = await actors.GetOrCreateAsync(
                actorId,
                YieldDispatchNames.ActorType,
                cancellationToken);
            evidence.Add(
                $"bind-actor|rid={node.Rid}|spot={request.SpotRid}|actor={actor.ActorId}"
                + $"|generation={actor.Generation}");
            bindings.Add(new YieldActorBinding(
                actor.ActorId,
                actor.NodeRid.ToString(),
                actor.Generation));
        }

        return new BindYieldActorsReply(request.SpotRid, bindings.ToArray());
    }
}

internal sealed class EnsureSpotControlHandler(
    IZLinkSpotManager spots,
    NodeOptions node)
    : IZLinkRouteRequestHandler<EnsureSpotReq, EnsureSpotReply>
{
    public async ValueTask<EnsureSpotReply> HandleAsync(
        EnsureSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await spots.GetOrCreateAsync<YieldProbeSpot>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        return new EnsureSpotReply(request.SpotRid, node.Rid);
    }
}

internal sealed class YieldEvidenceControlHandler(EvidenceStore evidence)
    : IZLinkRouteRequestHandler<YieldEvidenceReq, YieldEvidenceReply>
{
    public ValueTask<YieldEvidenceReply> HandleAsync(
        YieldEvidenceReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new YieldEvidenceReply(request.RequestId, evidence.Snapshot()));
    }
}

internal sealed class YieldEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot<YieldActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;
}

internal sealed class YieldActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new YieldActor(actorId, context));
    }
}

internal sealed class YieldActor(string actorId, IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;
}

[ZLinkSpotRequestHandler("HoldReq")]
internal sealed class HoldHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, HoldReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        HoldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"hold-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await spot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, "hold"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<DelayReply>(cancellationToken);
        evidence.Add($"hold-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add($"hold-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        return YieldReplies.Reply("YD-A1", request.RequestId, spot, "hold-completed");
    }
}

[ZLinkSpotRequestHandler("YieldReq")]
internal sealed class YieldHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, YieldReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        YieldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        var call = spot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, "yield"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        await call.Yield<DelayReply>(cancellationToken);
        evidence.Add(
            $"yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        evidence.Add(
            $"yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        return YieldReplies.Reply("YD-A2", request.RequestId, spot, "yield-completed");
    }
}

[ZLinkSpotRequestHandler("WorkerYieldReq")]
internal sealed class WorkerYieldHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, WorkerYieldReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        WorkerYieldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"worker-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        var call = spot.Context.RunWorker(ct =>
        {
            ct.ThrowIfCancellationRequested();
            Thread.Sleep(TimeSpan.FromMilliseconds(request.DelayMs));
            return request.RequestId;
        });
        evidence.Add($"worker-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await call.Yield(cancellationToken);
        evidence.Add($"worker-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add($"worker-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        return YieldReplies.Reply("YD-A4", request.RequestId, spot, "worker-yield-completed");
    }
}

[ZLinkSpotRequestHandler("YieldTimeoutReq")]
internal sealed class YieldTimeoutHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, YieldTimeoutReq, YieldTimeoutReply>
{
    public async ValueTask<YieldTimeoutReply> HandleAsync(
        YieldProbeSpot spot,
        YieldTimeoutReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"timeout-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "timeout"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs));
            evidence.Add($"timeout-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayReply>(cancellationToken);
            evidence.Add($"timeout-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            return new YieldTimeoutReply("YD-E1", request.RequestId, spot.Context.SpotRid.ToString(), spot.Context.NodeRid.ToString(), false, "");
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"timeout-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
            return new YieldTimeoutReply(
                "YD-E1",
                request.RequestId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                true,
                ex.GetType().Name);
        }
    }
}

[ZLinkSpotRequestHandler("YieldCancelReq")]
internal sealed class YieldCancelHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, YieldCancelReq, YieldCancelReply>
{
    public async ValueTask<YieldCancelReply> HandleAsync(
        YieldProbeSpot spot,
        YieldCancelReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"cancel-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(TimeSpan.FromMilliseconds(request.CancelAfterMs));
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "cancel"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromSeconds(5));
            evidence.Add($"cancel-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayReply>(cts.Token);
            evidence.Add($"cancel-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            return new YieldCancelReply("YD-E2", request.RequestId, spot.Context.SpotRid.ToString(), spot.Context.NodeRid.ToString(), false, "");
        }
        catch (Exception ex) when (ex is OperationCanceledException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"cancel-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
            return new YieldCancelReply(
                "YD-E2",
                request.RequestId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                true,
                ex.GetType().Name);
        }
    }
}

[ZLinkSpotRequestHandler("RemoteSpotYieldReq")]
internal sealed class RemoteSpotYieldHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, RemoteSpotYieldReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        RemoteSpotYieldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"remote-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        var call = spot.Context.Outbound.RequestToSpot(
                RoutingId.From(request.TargetSpotRid),
                new YieldReq(request.RequestId, request.DelayMs, "remote-spot"))
            .PacketName("YieldReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"remote-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        var targetReply = await call.Yield<YieldDispatchReply>(cancellationToken);
        evidence.Add(
            $"remote-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
        evidence.Add(
            $"remote-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
        return YieldReplies.Reply("YD-D2", request.RequestId, spot, "remote-yield-completed");
    }
}

[ZLinkSpotRequestHandler("ProbeReq")]
internal sealed class ProbeHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, ProbeReq, YieldDispatchReply>
{
    public ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        ProbeReq request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"probe-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=spot");
        evidence.Add(
            $"probe-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=spot");
        return ValueTask.FromResult(YieldReplies.Reply("YD-PROBE", request.RequestId, spot, request.Marker));
    }
}

[ZLinkSpotRequestHandler("TimerStartReq")]
internal sealed class TimerStartHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, TimerStartReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        TimerStartReq request,
        CancellationToken cancellationToken)
    {
        var state = new YieldTimerState(
            request.RequestId,
            request.TimerName,
            request.Mode,
            request.DelayMs);
        spot.AddTimerState(state);
        state.Timer = await spot.Context.AddTimer<YieldTimerHandler>(
            request.TimerName,
            TimeSpan.FromMilliseconds(request.PeriodMs),
            new ZLinkTimerOptions { OverrunPolicy = ZLinkTimerOverrunPolicy.DelayNextTick },
            cancellationToken);
        evidence.Add(
            $"timer-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|timer={request.TimerName}|mode={request.Mode}");
        return YieldReplies.Reply("YD-C", request.RequestId, spot, "timer-started");
    }
}

[ZLinkSpotRequestHandler("TimerStopReq")]
internal sealed class TimerStopHandler
    : IZLinkSpotRequestHandler<YieldProbeSpot, TimerStopReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        TimerStopReq request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        await spot.StopScenarioTimersAsync(request.RequestId);
        return YieldReplies.Reply("YD-C", request.RequestId, spot, "timer-stopped");
    }
}

internal sealed class YieldTimerHandler(EvidenceStore evidence)
    : IZLinkSpotTimerHandler<YieldProbeSpot>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        var state = spot.FindTimerState(tick.Name);
        if (state is null)
        {
            return;
        }

        var tickNumber = state.NextTick();
        if (string.Equals(state.Mode, "fast", StringComparison.Ordinal))
        {
            evidence.Add(
                $"timer-fast-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            evidence.Add(
                $"timer-fast-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            return;
        }

        if (tickNumber == 1
            && (string.Equals(state.Mode, "yield-on-first", StringComparison.Ordinal)
                || string.Equals(state.Mode, "yield-then-next", StringComparison.Ordinal)))
        {
            evidence.Add(
                $"timer-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(state.RequestId, state.DelayMs, state.TimerName))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromSeconds(5));
            evidence.Add(
                $"timer-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            await call.Yield<DelayReply>(cancellationToken);
            evidence.Add(
                $"timer-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            evidence.Add(
                $"timer-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            return;
        }

        if (string.Equals(state.Mode, "yield-then-next", StringComparison.Ordinal) && tickNumber == 2)
        {
            evidence.Add(
                $"timer-next-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
            evidence.Add(
                $"timer-next-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
                + $"|request={state.RequestId}|timer={state.TimerName}|tick={tickNumber}|handler=timer");
        }
    }
}

[ZLinkSpotActorRequestHandler("ActorYieldReq")]
internal sealed class EntryActorYieldHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorYieldReq, ActorYieldReply>
{
    public async ValueTask<ActorYieldReply> HandleAsync(
        YieldEntrySpot entrySpot,
        YieldActor actor,
        ZLinkSpotActorRequestContext context,
        ActorYieldReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        var mailboxId = $"actor:{actor.ActorId}";
        evidence.Add(
            $"actor-yield-started|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        var call = entrySpot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, $"actor-{actor.ActorId}"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"actor-yield-released|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        await call.Yield<DelayReply>(cancellationToken);
        evidence.Add(
            $"actor-yield-resumed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        evidence.Add(
            $"actor-yield-completed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        return ActorReplies.Reply("YD-B", request.RequestId, actor, entrySpot, "actor-yield-completed");
    }
}

[ZLinkSpotActorRequestHandler("ActorFastReq")]
internal sealed class EntryActorFastHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorFastReq, ActorYieldReply>
{
    public ValueTask<ActorYieldReply> HandleAsync(
        YieldEntrySpot entrySpot,
        YieldActor actor,
        ZLinkSpotActorRequestContext context,
        ActorFastReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        var mailboxId = $"actor:{actor.ActorId}";
        evidence.Add(
            $"actor-fast-started|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=actor");
        evidence.Add(
            $"actor-fast-completed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=actor");
        return ValueTask.FromResult(ActorReplies.Reply("YD-B", request.RequestId, actor, entrySpot, request.Marker));
    }
}

[ZLinkSpotActorRequestHandler("ActorJoinYieldReq")]
internal sealed class EntryActorJoinYieldHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorJoinYieldReq, ActorYieldReply>
{
    public async ValueTask<ActorYieldReply> HandleAsync(
        YieldEntrySpot entrySpot,
        YieldActor actor,
        ZLinkSpotActorRequestContext context,
        ActorJoinYieldReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        var mailboxId = $"actor:{actor.ActorId}";
        evidence.Add(
            $"actor-join-yield-started|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|target={request.TargetSpotRid}");
        var call = actor.Context.JoinSpot(
                RoutingId.From(request.TargetSpotRid),
                ZLinkMessage.From(new DelayReq(request.RequestId, 350, "join")));
        evidence.Add(
            $"actor-join-yield-released|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|target={request.TargetSpotRid}");
        var joined = await call.Yield(cancellationToken);
        evidence.Add(
            $"actor-join-yield-resumed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|accepted={joined.Accepted}");
        evidence.Add(
            $"actor-join-yield-completed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|accepted={joined.Accepted}");
        return ActorReplies.Reply("YD-B3", request.RequestId, actor, entrySpot, "actor-join-yield-completed");
    }
}

[ZLinkSpotActorRequestHandler("ActorPushYieldReq")]
internal sealed class EntryActorPushYieldHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorPushYieldReq, ActorYieldReply>
{
    public async ValueTask<ActorYieldReply> HandleAsync(
        YieldEntrySpot entrySpot,
        YieldActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPushYieldReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        var mailboxId = $"actor:{actor.ActorId}";
        evidence.Add(
            $"actor-push-yield-started|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        var call = entrySpot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, $"actor-push-{actor.ActorId}"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"actor-push-yield-released|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        await call.Yield<DelayReply>(cancellationToken);
        evidence.Add(
            $"actor-push-yield-resumed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        await actor.Context.BoundSession.Send(
                new ActorPushNotify(
                    actor.ActorId,
                    request.RequestId,
                    request.Value,
                    entrySpot.Context.NodeRid.ToString()))
            .PacketName("ActorPushNotify")
            .Async(cancellationToken);
        evidence.Add(
            $"actor-push-yield-completed|rid={evidence.Rid}|spot={entrySpot.Context.SpotRid}"
            + $"|actor={actor.ActorId}|mailbox={mailboxId}|request={request.RequestId}|handler=actor");
        return ActorReplies.Reply("YD-D4", request.RequestId, actor, entrySpot, "actor-push-yield-completed");
    }
}

internal sealed record NodeOptions(string Rid);

internal sealed class EvidenceStore
{
    private readonly object _gate = new();
    private readonly List<string> _entries = [];
    private readonly string? _filePath;

    public EvidenceStore(string rid, string? filePath)
    {
        Rid = rid;
        _filePath = filePath;
    }

    public string Rid { get; }

    public void Add(string entry)
    {
        lock (_gate)
        {
            _entries.Add(entry);
            if (!string.IsNullOrWhiteSpace(_filePath))
            {
                File.AppendAllText(_filePath, entry + Environment.NewLine);
            }
        }
    }

    public string[] Snapshot()
    {
        lock (_gate)
        {
            return _entries.ToArray();
        }
    }
}

internal sealed record PlayOptions(
    string Rid,
    string HttpUrl,
    string RegistryRouterEndpoint,
    string ControlEndpoint,
    string DelayEndpoint,
    string SpotRouterEndpoint,
    string SpotPubEndpoint,
    string SpotRouteEndpoint,
    string LogDir)
{
    public string EvidenceFile => Path.Combine(LogDir, $"{Rid}.evidence.log");

    public static PlayOptions Parse(string[] args)
    {
        var values = Cli.Parse(args);
        return new PlayOptions(
            Cli.Required(values, "rid"),
            Cli.Required(values, "http-url"),
            Cli.Required(values, "registry-router-endpoint"),
            Cli.Required(values, "control-endpoint"),
            Cli.Required(values, "delay-endpoint"),
            Cli.Required(values, "spot-router-endpoint"),
            Cli.Required(values, "spot-pub-endpoint"),
            Cli.Required(values, "spot-route-endpoint"),
            Cli.Required(values, "log-dir"));
    }
}

internal static class Cli
{
    public static Dictionary<string, string> Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            if (!args[i].StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {args[i]}.");
            }

            values[args[i][2..]] = args[++i];
        }

        return values;
    }

    public static string Required(Dictionary<string, string> values, string key)
        => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");
}

internal static class YieldReplies
{
    public static YieldDispatchReply Reply(
        string scenarioId,
        string requestId,
        YieldProbeSpot spot,
        string marker)
    {
        return new YieldDispatchReply(
            scenarioId,
            requestId,
            spot.Context.SpotRid.ToString(),
            spot.Context.NodeRid.ToString(),
            marker);
    }
}

internal static class ActorReplies
{
    public static ActorYieldReply Reply(
        string scenarioId,
        string requestId,
        YieldActor actor,
        YieldEntrySpot entrySpot,
        string marker)
    {
        return new ActorYieldReply(
            scenarioId,
            requestId,
            actor.ActorId,
            entrySpot.Context.SpotRid.ToString(),
            entrySpot.Context.NodeRid.ToString(),
            marker);
    }
}
