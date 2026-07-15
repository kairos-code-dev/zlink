using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class SingleTerminatorProbe
{
    public static async Task<(string SpotRid, string RequestId)> RunAsync(IZlinkStreamConnector client)
    {
        var spotRid = $"await-track-a-{Guid.NewGuid():N}";
        var spot = await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotRes>();
        ZlinkStreamAssert.Ensure(spot.SpotRid == spotRid, "probe-A spot creation mismatch.");

        var requestId = $"probe-A1-{Guid.NewGuid():N}";
        client.Send(new HoldMsg(requestId, 350))
            .PacketName("HoldMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "hold-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        EvidenceOrder.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "hold-started",
            "hold-resumed",
            "hold-completed"
        ]);
        return (spotRid, requestId);
    }
}
