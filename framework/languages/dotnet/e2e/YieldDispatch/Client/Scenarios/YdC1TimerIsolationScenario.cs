using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdC1TimerIsolationScenario
{
    public static async Task<(string SpotRid, string RequestId)> RunAsync(YieldDispatchScenarioContext context)
    {
        var spotRid = $"yield-timer-{Guid.NewGuid():N}";
        var spot = await context.Client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();
        ScenarioAssert.That(spot.SpotRid == spotRid, "YD-C timer spot creation mismatch.");

        var requestId = $"YD-C1-{Guid.NewGuid():N}";
        await context.Client.Send(new TimerStartCommand(requestId, $"{requestId}-yield", "yield-on-first", 50, 350))
            .PacketName("TimerStartCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await context.WaitForPlayEvidenceAsync(requestId, "timer-yield-released");
        await context.Client.Send(new TimerStartCommand(requestId, $"{requestId}-fast", "fast", 50, 0))
            .PacketName("TimerStartCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await context.WaitForPlayEvidenceAsync(requestId, "timer-fast-completed");
        var evidence = await context.WaitForPlayEvidenceAsync(requestId, "timer-yield-completed");
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "timer-yield-started",
            "timer-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"]);
        await context.Client.Send(new TimerStopCommand(requestId))
            .PacketName("TimerStopCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        return (spotRid, requestId);
    }
}
