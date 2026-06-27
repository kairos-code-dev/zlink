using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdC2TimerReentryScenario
{
    public static async Task<string> RunAsync(IZlinkStreamConnector client, string spotRid)
    {
        var requestId = $"YD-C2-{Guid.NewGuid():N}";
        await client.Send(new TimerStartCommand(requestId, $"{requestId}-same", "yield-then-next", 50, 350))
            .PacketName("TimerStartCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "timer-next-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed",
            "timer-next-started",
            "timer-next-completed"]);
        await client.Send(new TimerStopCommand(requestId))
            .PacketName("TimerStopCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        return requestId;
    }
}
