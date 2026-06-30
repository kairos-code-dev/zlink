using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdA2YieldTerminatorScenario
{
    public static async Task<string> RunAsync(IZlinkStreamConnector client, string spotRid)
    {
        var requestId = $"YD-A2-{Guid.NewGuid():N}";
        await client.Send(new YieldMsg(requestId, 350, "corr-a2"))
            .PacketName("YieldMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await client.Request(new YieldEvidenceWaitReq(requestId, "yield-released"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        await client.Send(new ProbeMsg(requestId, "yield-probe"))
            .PacketName("ProbeMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"
        ]);
        return requestId;
    }
}