using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdA4WorkerYieldScenario
{
    public static async Task<string> RunAsync(IZlinkStreamConnector client, string spotRid)
    {
        var requestId = $"YD-A4-{Guid.NewGuid():N}";
        await client.Send(new WorkerYieldMsg(requestId, 350))
            .PacketName("WorkerYieldMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await client.Request(new YieldEvidenceWaitReq(requestId, "worker-yield-released"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        await client.Send(new ProbeMsg(requestId, "worker-probe"))
            .PacketName("ProbeMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "worker-yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "worker-yield-started",
            "worker-yield-released",
            "probe-started",
            "probe-completed",
            "worker-yield-resumed",
            "worker-yield-completed"
        ]);
        return requestId;
    }
}