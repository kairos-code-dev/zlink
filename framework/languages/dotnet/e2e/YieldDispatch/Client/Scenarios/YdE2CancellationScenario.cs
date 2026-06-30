using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdE2CancellationScenario
{
    public static async Task RunAsync(IZlinkStreamConnector client)
    {
        var spotRid = $"yield-cancel-{Guid.NewGuid():N}";
        var spot = await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();
        ScenarioAssert.That(spot.SpotRid == spotRid, "YD-E2 spot creation mismatch.");

        var requestId = $"YD-E2-{Guid.NewGuid():N}";
        await client.Send(new YieldCancelCommand(requestId, 800, 100))
            .PacketName("YieldCancelCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await client.Request(new YieldEvidenceWaitReq(requestId, "cancel-yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        await client.Send(new ProbeCommand(requestId, "cancel-probe"))
            .PacketName("ProbeCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "probe-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "cancel-yield-started",
            "cancel-yield-released",
            "cancel-yield-completed",
            "probe-started",
            "probe-completed"
        ]);
    }
}