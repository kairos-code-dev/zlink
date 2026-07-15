using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class ActorTimerIsolationProbe
{
    public static async Task RunAsync(
        IZlinkStreamConnector client,
        AwaitActorScenarioContext actors)
    {
        var actorThenTimer = $"probe-C3A-{Guid.NewGuid():N}";
        var actorAwait = client.Request(new ActorAwaitReq(actorThenTimer, 350))
            .PacketName("ActorAwaitReq")
            .Metadata(AutomaticTurnDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorAwaitRes>();
        await Task.Delay(75);
        client.Send(new TimerStartMsg(actorThenTimer, $"{actorThenTimer}-fast", "fast", 50, 0))
            .PacketName("TimerStartMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, actors.SpotRid).Submit();
        await client.Request(new AwaitEvidenceWaitReq(actorThenTimer, "timer-fast-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        client.Send(new TimerStopMsg(actorThenTimer))
            .PacketName("TimerStopMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, actors.SpotRid).Submit();
        await actorAwait;

        var actorThenTimerEvidence = await client.Request(new AwaitEvidenceReq(actorThenTimer))
            .PacketName("AwaitEvidenceReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        EvidenceOrder.ContainsExactRequestInOrder(actorThenTimerEvidence.Evidence, actorThenTimer, [
            "actor-await-started",
            "actor-await-released",
            "timer-fast-started",
            "timer-fast-completed",
            "actor-await-resumed",
            "actor-await-completed"
        ]);

        var timerThenActor = $"probe-C3B-{Guid.NewGuid():N}";
        client.Send(new TimerStartMsg(timerThenActor, $"{timerThenActor}-await", "await-on-first", 50, 350))
            .PacketName("TimerStartMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, actors.SpotRid).Submit();
        await client.Request(new AwaitEvidenceWaitReq(timerThenActor, "timer-await-released"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        await client.Request(new ActorFastReq(timerThenActor, "c3-actor-fast"))
            .PacketName("ActorFastReq")
            .Metadata(AutomaticTurnDispatchNames.ActorIdMetadata, actors.ActorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorAwaitRes>();
        var timerThenActorEvidence = await client
            .Request(new AwaitEvidenceWaitReq(timerThenActor, "timer-await-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        client.Send(new TimerStopMsg(timerThenActor))
            .PacketName("TimerStopMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, actors.SpotRid).Submit();

        EvidenceOrder.ContainsExactRequestInOrder(timerThenActorEvidence.Evidence, timerThenActor, [
            "timer-await-started",
            "timer-await-released",
            "actor-fast-started",
            "actor-fast-completed",
            "timer-await-resumed",
            "timer-await-completed"
        ]);
    }
}
