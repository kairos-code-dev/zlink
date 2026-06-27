using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdC3ActorTimerIsolationScenario
{
    public static async Task RunAsync(
        YieldDispatchScenarioContext context,
        YieldActorScenarioContext actors)
    {
        var actorThenTimer = $"YD-C3A-{Guid.NewGuid():N}";
        var actorYield = context.Client.Request(new ActorYieldReq(actorThenTimer, 350))
            .PacketName("ActorYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        await context.Client.Send(new TimerStartCommand(actorThenTimer, $"{actorThenTimer}-fast", "fast", 50, 0))
            .PacketName("TimerStartCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid)
            .Async();
        await context.WaitForPlayEvidenceAsync(actorThenTimer, "timer-fast-completed");
        await context.Client.Send(new TimerStopCommand(actorThenTimer))
            .PacketName("TimerStopCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid)
            .Async();
        await actorYield;

        var actorThenTimerEvidence = await context.ReadPlayEvidenceAsync(actorThenTimer, "play-a");
        ScenarioAssert.ContainsExactRequestInOrder(actorThenTimerEvidence.Evidence, actorThenTimer, [
            "actor-yield-started",
            "actor-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed"]);

        var timerThenActor = $"YD-C3B-{Guid.NewGuid():N}";
        await context.Client.Send(new TimerStartCommand(timerThenActor, $"{timerThenActor}-yield", "yield-on-first", 50, 350))
            .PacketName("TimerStartCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid)
            .Async();
        await context.WaitForPlayEvidenceAsync(timerThenActor, "timer-yield-released");
        await context.Client.Request(new ActorFastReq(timerThenActor, "c3-actor-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        var timerThenActorEvidence = await context.WaitForPlayEvidenceAsync(timerThenActor, "timer-yield-completed");
        await context.Client.Send(new TimerStopCommand(timerThenActor))
            .PacketName("TimerStopCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, actors.SpotRid)
            .Async();

        ScenarioAssert.ContainsExactRequestInOrder(timerThenActorEvidence.Evidence, timerThenActor, [
            "timer-yield-started",
            "timer-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"]);
    }
}
