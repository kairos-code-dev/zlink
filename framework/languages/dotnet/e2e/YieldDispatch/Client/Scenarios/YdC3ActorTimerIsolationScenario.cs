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
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        await client.Send(new TimerStartCommand(actorThenTimer, $"{actorThenTimer}-fast", "fast", 50, 0))
            .PacketName("TimerStartCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid)
            .Async();
        await client.Request(new YieldEvidenceWaitReq(actorThenTimer, "timer-fast-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        await client.Send(new TimerStopCommand(actorThenTimer))
            .PacketName("TimerStopCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid)
            .Async();
        await actorYield;

        var actorThenTimerEvidence = await client.Request(new YieldEvidenceReq(actorThenTimer))
            .PacketName("YieldEvidenceReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsExactRequestInOrder(actorThenTimerEvidence.Evidence, actorThenTimer, [
            "actor-yield-started",
            "actor-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed"
        ]);

        var timerThenActor = $"YD-C3B-{Guid.NewGuid():N}";
        await client.Send(new TimerStartCommand(timerThenActor, $"{timerThenActor}-yield", "yield-on-first", 50, 350))
            .PacketName("TimerStartCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid)
            .Async();
        await client.Request(new YieldEvidenceWaitReq(timerThenActor, "timer-yield-released"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        await client.Request(new ActorFastReq(timerThenActor, "c3-actor-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        var timerThenActorEvidence = await client
            .Request(new YieldEvidenceWaitReq(timerThenActor, "timer-yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        await client.Send(new TimerStopCommand(timerThenActor))
            .PacketName("TimerStopCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid)
            .Async();

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