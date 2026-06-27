using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdA1BasicTerminatorScenario
{
    public static async Task<(string SpotRid, string RequestId)> RunAsync(IZlinkStreamConnector client)
    {
        var spotRid = $"yield-track-a-{Guid.NewGuid():N}";
        var spot = await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();
        ScenarioAssert.That(spot.SpotRid == spotRid, "YD-A spot creation mismatch.");

        var requestId = $"YD-A1-{Guid.NewGuid():N}";
        await client.Send(new HoldCommand(requestId, 350))
            .PacketName("HoldCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await Task.Delay(75);
        await client.Send(new ProbeCommand(requestId, "hold-probe"))
            .PacketName("ProbeCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();

        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "probe-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "hold-started",
            "hold-resumed",
            "hold-completed",
            "probe-started"]);
        return (spotRid, requestId);
    }
}
