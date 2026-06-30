using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdC2TimerReentryScenario
{
    public static async Task<string> RunAsync(IZlinkStreamConnector client, string spotRid)
    {
        var requestId = $"YD-C2-{Guid.NewGuid():N}";
        client.Send(new TimerStartMsg(requestId, $"{requestId}-same", "yield-then-next", 50, 350))
            .PacketName("TimerStartMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "timer-next-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed",
            "timer-next-started",
            "timer-next-completed"
        ]);
        client.Send(new TimerStopMsg(requestId))
            .PacketName("TimerStopMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid).Submit();
        return requestId;
    }
}