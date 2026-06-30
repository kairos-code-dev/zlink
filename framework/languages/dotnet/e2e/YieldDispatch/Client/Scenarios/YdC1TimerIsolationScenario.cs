using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdC1TimerIsolationScenario
{
    public static async Task<(string SpotRid, string RequestId)> RunAsync(IZlinkStreamConnector client)
    {
        var spotRid = $"yield-timer-{Guid.NewGuid():N}";
        var spot = await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotRes>();
        ScenarioAssert.That(spot.SpotRid == spotRid, "YD-C timer spot creation mismatch.");

        var requestId = $"YD-C1-{Guid.NewGuid():N}";
        client.Send(new TimerStartMsg(requestId, $"{requestId}-yield", "yield-on-first", 50, 350))
            .PacketName("TimerStartMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid).Submit();
        await client.Request(new YieldEvidenceWaitReq(requestId, "timer-yield-released"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        client.Send(new TimerStartMsg(requestId, $"{requestId}-fast", "fast", 50, 0))
            .PacketName("TimerStartMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid).Submit();
        await client.Request(new YieldEvidenceWaitReq(requestId, "timer-fast-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "timer-yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "timer-yield-started",
            "timer-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"
        ]);
        client.Send(new TimerStopMsg(requestId))
            .PacketName("TimerStopMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid).Submit();
        return (spotRid, requestId);
    }
}