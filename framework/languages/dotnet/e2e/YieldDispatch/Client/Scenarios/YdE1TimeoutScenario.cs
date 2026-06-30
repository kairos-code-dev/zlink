using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdE1TimeoutScenario
{
    public static async Task RunAsync(IZlinkStreamConnector client)
    {
        var spotRid = $"yield-timeout-{Guid.NewGuid():N}";
        var spot = await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotRes>();
        ScenarioAssert.That(spot.SpotRid == spotRid, "YD-E1 spot creation mismatch.");

        var requestId = $"YD-E1-{Guid.NewGuid():N}";
        await client.Send(new YieldTimeoutMsg(requestId, 700, 100))
            .PacketName("YieldTimeoutMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await client.Request(new YieldEvidenceWaitReq(requestId, "timeout-yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        await client.Send(new ProbeMsg(requestId, "timeout-probe"))
            .PacketName("ProbeMsg")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "probe-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        ScenarioAssert.That(
            evidence.Evidence.Any(line => line.Contains($"request={requestId}", StringComparison.Ordinal)
                                          && line.Contains("timeout-yield-completed", StringComparison.Ordinal)),
            "YD-E1 timeout marker missing.");
        ScenarioAssert.That(
            evidence.Evidence.Any(line => line.Contains($"request={requestId}", StringComparison.Ordinal)
                                          && line.Contains("probe-completed", StringComparison.Ordinal)
                                          && line.Contains("timeout-probe", StringComparison.Ordinal)),
            "YD-E1 post-timeout probe marker missing.");
    }
}