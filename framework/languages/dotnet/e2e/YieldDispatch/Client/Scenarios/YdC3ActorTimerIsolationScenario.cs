using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdC3ActorTimerIsolationScenario
{
    public static async Task RunAsync(
        IZlinkStreamConnector client,
        YieldActorScenarioContext actors)
    {
        var actorThenTimer = $"YD-C3A-{Guid.NewGuid():N}";
        var actorYield = client.Request(new ActorYieldReq(actorThenTimer, 350))
            .PacketName("ActorYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldRes>();
        await Task.Delay(75);
        client.Send(new TimerStartMsg(actorThenTimer, $"{actorThenTimer}-fast", "fast", 50, 0))
            .PacketName("TimerStartMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid).Submit();
        await client.Request(new YieldEvidenceWaitReq(actorThenTimer, "timer-fast-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        client.Send(new TimerStopMsg(actorThenTimer))
            .PacketName("TimerStopMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid).Submit();
        await actorYield;

        var actorThenTimerEvidence = await client.Request(new YieldEvidenceReq(actorThenTimer))
            .PacketName("YieldEvidenceReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(actorThenTimerEvidence.Evidence, actorThenTimer, [
            "actor-yield-started",
            "actor-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed"
        ]);

        var timerThenActor = $"YD-C3B-{Guid.NewGuid():N}";
        client.Send(new TimerStartMsg(timerThenActor, $"{timerThenActor}-yield", "yield-on-first", 50, 350))
            .PacketName("TimerStartMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid).Submit();
        await client.Request(new YieldEvidenceWaitReq(timerThenActor, "timer-yield-released"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        await client.Request(new ActorFastReq(timerThenActor, "c3-actor-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldRes>();
        var timerThenActorEvidence = await client
            .Request(new YieldEvidenceWaitReq(timerThenActor, "timer-yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        client.Send(new TimerStopMsg(timerThenActor))
            .PacketName("TimerStopMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid).Submit();

        ScenarioAssert.ContainsExactRequestInOrder(timerThenActorEvidence.Evidence, timerThenActor, [
            "timer-yield-started",
            "timer-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"
        ]);
    }
}