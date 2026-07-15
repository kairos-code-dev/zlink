using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class CancellationProbe
{
    public static async Task RunAsync(IZlinkStreamConnector client)
    {
        var spotRid = $"await-cancel-{Guid.NewGuid():N}";
        var spot = await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotRes>();
        ZlinkStreamAssert.Ensure(spot.SpotRid == spotRid, "probe-E2 spot creation mismatch.");

        var requestId = $"probe-E2-{Guid.NewGuid():N}";
        client.Send(new AwaitCancelMsg(requestId, 800, 100))
            .PacketName("AwaitCancelMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        await client.Request(new AwaitEvidenceWaitReq(requestId, "cancel-await-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        client.Send(new ProbeMsg(requestId, "cancel-probe"))
            .PacketName("ProbeMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "probe-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        EvidenceOrder.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "cancel-await-started",
            "cancel-await-released",
            "cancel-await-completed",
            "probe-started",
            "probe-completed"
        ]);
    }
}
