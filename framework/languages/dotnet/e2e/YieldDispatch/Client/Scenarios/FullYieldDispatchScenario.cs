using Systems.Zlink.Stream.Connector.Contracts;
using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class FullYieldDispatchScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await using var client = YieldConnectorFactory.Create(options.SessionAStreamEndpoint);
        await client.Connect.Async();

        var trackA = await client.Request(new YieldTrackAReq($"client-track-a-{Guid.NewGuid():N}"))
            .PacketName("YieldTrackAReq")
            .Timeout(TimeSpan.FromSeconds(60))
            .Async<YieldScenarioResult>();
        ScenarioAssert.That(trackA.Operation == "yield.track-a", "YD-A operation mismatch.");
        ScenarioAssert.ContainsInOrder(trackA.Evidence, "YD-A1-", [
            "hold-started",
            "hold-resumed",
            "hold-completed",
            "probe-started"]);
        ScenarioAssert.ContainsInOrder(trackA.Evidence, "YD-A2-", [
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"]);
        ScenarioAssert.ContainsInOrder(trackA.Evidence, "YD-A4-", [
            "worker-yield-started",
            "worker-yield-released",
            "probe-started",
            "probe-completed",
            "worker-yield-resumed",
            "worker-yield-completed"]);

        var timeout = await client.Request(new YieldTimeoutScenarioReq($"client-timeout-{Guid.NewGuid():N}"))
            .PacketName("YieldTimeoutScenarioReq")
            .Timeout(TimeSpan.FromSeconds(60))
            .Async<YieldScenarioResult>();
        ScenarioAssert.That(timeout.Operation == "yield.e1-timeout", "YD-E1 operation mismatch.");
        ScenarioAssert.That(
            timeout.Evidence.Any(line => line.Contains("timeout-yield-completed", StringComparison.Ordinal)),
            "YD-E1 timeout marker missing.");
        ScenarioAssert.That(
            timeout.Evidence.Any(line => line.Contains("probe-completed", StringComparison.Ordinal)
                && line.Contains("timeout-probe", StringComparison.Ordinal)),
            "YD-E1 post-timeout probe marker missing.");

        var timer = await client.Request(new YieldTimerScenarioReq($"client-timer-{Guid.NewGuid():N}"))
            .PacketName("YieldTimerScenarioReq")
            .Timeout(TimeSpan.FromSeconds(60))
            .Async<YieldScenarioResult>();
        ScenarioAssert.That(timer.Operation == "yield.track-c-timer", "YD-C operation mismatch.");
        ScenarioAssert.ContainsInOrder(timer.Evidence, "YD-C1-", [
            "timer-yield-started",
            "timer-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"]);
        ScenarioAssert.ContainsInOrder(timer.Evidence, "YD-C2-", [
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed",
            "timer-next-started",
            "timer-next-completed"]);

        var remote = await client.Request(new YieldRemoteScenarioReq($"client-remote-{Guid.NewGuid():N}", "play-b"))
            .PacketName("YieldRemoteScenarioReq")
            .Timeout(TimeSpan.FromSeconds(60))
            .Async<YieldScenarioResult>();
        ScenarioAssert.That(remote.Operation == "yield.d2-remote", "YD-D2 operation mismatch.");
        ScenarioAssert.ContainsInOrder(remote.Evidence, "YD-D2-", [
            "remote-yield-started",
            "remote-yield-released",
            "probe-started",
            "probe-completed",
            "remote-yield-resumed",
            "remote-yield-completed"]);
        ScenarioAssert.That(
            remote.Evidence.Any(line => line.Contains("yield-started|rid=play-b", StringComparison.Ordinal)),
            "YD-D2 target play-b marker missing.");

        var routeBridge = await client.Request(new YieldRouteBridgeScenarioReq($"YD-D3-{Guid.NewGuid():N}", "play-b"))
            .PacketName("YieldRouteBridgeScenarioReq")
            .Timeout(TimeSpan.FromSeconds(60))
            .Async<YieldScenarioResult>();
        ScenarioAssert.That(routeBridge.Operation == "yield.d3-route-bridge", "YD-D3 operation mismatch.");
        ScenarioAssert.ContainsInOrder(routeBridge.Evidence, "YD-D3-", [
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"]);
        ScenarioAssert.That(
            routeBridge.Evidence.Any(line => line.Contains("yield-started|rid=play-b", StringComparison.Ordinal)
                && line.Contains("handler=spot", StringComparison.Ordinal)),
            "YD-D3 target Spot handler marker missing.");

        var cancellation = await client.Request(new YieldCancellationScenarioReq($"client-cancel-{Guid.NewGuid():N}"))
            .PacketName("YieldCancellationScenarioReq")
            .Timeout(TimeSpan.FromSeconds(60))
            .Async<YieldScenarioResult>();
        ScenarioAssert.That(cancellation.Operation == "yield.e2-cancellation", "YD-E2 operation mismatch.");
        ScenarioAssert.ContainsInOrder(cancellation.Evidence, "YD-E2-", [
            "cancel-yield-started",
            "cancel-yield-released",
            "cancel-yield-completed",
            "probe-started",
            "probe-completed"]);

        var actorSpotRid = $"yield-track-b-{Guid.NewGuid():N}";
        var actorA = $"actor-a-{Guid.NewGuid():N}";
        var actorB = $"actor-b-{Guid.NewGuid():N}";
        var bound = await client.Request(new BindYieldActorsReq(actorSpotRid, [actorA, actorB]))
            .PacketName("BindYieldActorsReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<BindYieldActorsReply>();
        ScenarioAssert.That(bound.Actors.Length == 2, "YD-B bind actor count mismatch.");

        var b1 = $"YD-B1-{Guid.NewGuid():N}";
        var b1Yield = client.Request(new ActorYieldReq(b1, 350))
            .PacketName("ActorYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        var b1Fast = client.Request(new ActorFastReq(b1, "b1-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.WhenAll(b1Yield.AsTask(), b1Fast.AsTask());

        var b2 = $"YD-B2-{Guid.NewGuid():N}";
        var b2Yield = client.Request(new ActorYieldReq(b2, 350))
            .PacketName("ActorYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        var b2Fast = client.Request(new ActorFastReq(b2, "b2-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.WhenAll(b2Yield.AsTask(), b2Fast.AsTask());

        var b3 = $"YD-B3-{Guid.NewGuid():N}";
        var b3Join = client.Request(new ActorJoinYieldReq(b3, actorSpotRid))
            .PacketName("ActorJoinYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        var b3Fast = client.Request(new ActorFastReq(b3, "b3-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.WhenAll(b3Join.AsTask(), b3Fast.AsTask());

        var c3ActorTimer = $"YD-C3A-{Guid.NewGuid():N}";
        var c3ActorYield = client.Request(new ActorYieldReq(c3ActorTimer, 350))
            .PacketName("ActorYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        var c3TimerFast = await client.Request(
                new YieldActorTimerScenarioReq(c3ActorTimer, actorSpotRid, $"{c3ActorTimer}-fast", "start-fast", 50, 0))
            .PacketName("YieldActorTimerScenarioReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldScenarioResult>();
        ScenarioAssert.That(c3TimerFast.Operation == "yield.c3-actor-timer", "YD-C3 timer-fast operation mismatch.");
        await c3ActorYield;

        var c3TimerActor = $"YD-C3B-{Guid.NewGuid():N}";
        var c3TimerYield = await client.Request(
                new YieldActorTimerScenarioReq(c3TimerActor, actorSpotRid, $"{c3TimerActor}-yield", "start-yield", 50, 350))
            .PacketName("YieldActorTimerScenarioReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldScenarioResult>();
        ScenarioAssert.That(c3TimerYield.Operation == "yield.c3-actor-timer", "YD-C3 timer-yield operation mismatch.");
        await client.Request(new ActorFastReq(c3TimerActor, "c3-actor-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await client.Request(
                new YieldActorTimerScenarioReq(c3TimerActor, actorSpotRid, $"{c3TimerActor}-yield", "wait-stop", 50, 0))
            .PacketName("YieldActorTimerScenarioReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldScenarioResult>();

        await using var unbound = YieldConnectorFactory.Create(options.SessionBStreamEndpoint);
        await unbound.Connect.Async();
        var d4 = $"YD-D4-{Guid.NewGuid():N}";
        var d4Push = client.WaitFor<ActorPushNotify>()
            .Timeout(TimeSpan.FromSeconds(30))
            .Async()
            .AsTask();
        var d4Reply = await client.Request(new ActorPushYieldReq(d4, 250, "bound-session-push"))
            .PacketName("ActorPushYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        var d4Notify = await d4Push;
        ScenarioAssert.That(d4Reply.ScenarioId == "YD-D4", "YD-D4 reply scenario mismatch.");
        ScenarioAssert.That(d4Notify.Payload.ActorId == actorA, "YD-D4 push actor mismatch.");
        ScenarioAssert.That(d4Notify.Payload.RequestId == d4, "YD-D4 push request mismatch.");
        ScenarioAssert.That(d4Notify.Payload.Value == "bound-session-push", "YD-D4 push value mismatch.");
        await Task.Delay(150);
        ScenarioAssert.That(unbound.ReceivedCount("ActorPushNotify") == 0, "YD-D4 unbound session received actor push.");

        var evidence = await client.Request(new YieldEvidenceReq($"client-evidence-{Guid.NewGuid():N}"))
            .PacketName("YieldEvidenceReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, b1, [
            "actor-yield-started",
            "actor-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed"]);
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, b2, [
            "actor-yield-started",
            "actor-yield-released",
            "actor-yield-resumed",
            "actor-yield-completed",
            "actor-fast-started",
            "actor-fast-completed"]);
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, b3, [
            "actor-join-yield-started",
            "actor-join-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-join-yield-resumed",
            "actor-join-yield-completed"]);
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, c3ActorTimer, [
            "actor-yield-started",
            "actor-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed"]);
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, c3TimerActor, [
            "timer-yield-started",
            "timer-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"]);
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, d4, [
            "actor-push-yield-started",
            "actor-push-yield-released",
            "actor-push-yield-resumed",
            "actor-push-yield-completed"]);

        Console.WriteLine("yield-dispatch client result=passed");
    }
}
