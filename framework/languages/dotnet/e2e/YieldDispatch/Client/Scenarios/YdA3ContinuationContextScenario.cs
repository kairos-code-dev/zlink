using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdA3ContinuationContextScenario
{
    public static async Task<string> RunAsync(IZlinkStreamConnector client, string spotRid)
    {
        var requestId = $"YD-A3-{Guid.NewGuid():N}";
        client.Send(new YieldMsg(requestId, 50, "corr-a3"))
            .PacketName("YieldMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "yield-started",
            "yield-released",
            "yield-resumed",
            "yield-completed"
        ]);
        return requestId;
    }
}