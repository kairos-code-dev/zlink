using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdA3ContinuationContextScenario
{
    public static async Task<string> RunAsync(IZlinkStreamConnector client, string spotRid)
    {
        var requestId = $"YD-A3-{Guid.NewGuid():N}";
        await client.Send(new YieldCommand(requestId, 50, "corr-a3"))
            .PacketName("YieldCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "yield-started",
            "yield-released",
            "yield-resumed",
            "yield-completed"
        ]);
        return requestId;
    }
}