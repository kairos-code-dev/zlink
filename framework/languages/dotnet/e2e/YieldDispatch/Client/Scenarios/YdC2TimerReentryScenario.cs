using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdC2TimerReentryScenario
{
    public static async Task<string> RunAsync(YieldDispatchScenarioContext context, string spotRid)
    {
        var requestId = $"YD-C2-{Guid.NewGuid():N}";
        await context.Client.Send(new TimerStartCommand(requestId, $"{requestId}-same", "yield-then-next", 50, 350))
            .PacketName("TimerStartCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await context.WaitForPlayEvidenceAsync(requestId, "timer-next-completed");
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed",
            "timer-next-started",
            "timer-next-completed"]);
        await context.Client.Send(new TimerStopCommand(requestId))
            .PacketName("TimerStopCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        return requestId;
    }
}
