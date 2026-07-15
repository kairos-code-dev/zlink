using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class RouteBridgeAwaitProbe
{
    public static async Task RunAsync(IZlinkStreamConnector client)
    {
        var requestId = $"probe-D3-{Guid.NewGuid():N}";
        var spotRid = $"await-route-bridge-{Guid.NewGuid():N}";
        await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotRes>();
        client.Send(new AwaitMsg(requestId, 250, "route-bridge"))
            .PacketName("AwaitMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        await Task.Delay(75);
        client.Send(new ProbeMsg(requestId, "route-bridge-probe"))
            .PacketName("ProbeMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "await-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        EvidenceOrder.ContainsInOrder(evidence.Evidence, requestId, [
            "await-started",
            "await-released",
            "probe-started",
            "probe-completed",
            "await-resumed",
            "await-completed"
        ]);
        ZlinkStreamAssert.Ensure(
            evidence.Evidence.Any(line => line.Contains("await-started|rid=play-b", StringComparison.Ordinal)
                                          && line.Contains("handler=spot", StringComparison.Ordinal)),
            "probe-D3 target Spot handler marker missing.");
    }
}
