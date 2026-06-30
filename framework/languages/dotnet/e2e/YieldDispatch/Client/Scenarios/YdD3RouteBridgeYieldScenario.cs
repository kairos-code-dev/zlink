using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdD3RouteBridgeYieldScenario
{
    public static async Task RunAsync(IZlinkStreamConnector client)
    {
        var requestId = $"YD-D3-{Guid.NewGuid():N}";
        var spotRid = $"yield-route-bridge-{Guid.NewGuid():N}";
        await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();
        await client.Send(new YieldCommand(requestId, 250, "route-bridge"))
            .PacketName("YieldCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await Task.Delay(75);
        await client.Send(new ProbeCommand(requestId, "route-bridge-probe"))
            .PacketName("ProbeCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await client.Request(new YieldEvidenceWaitReq(requestId, "yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsInOrder(evidence.Evidence, requestId, [
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"
        ]);
        ScenarioAssert.That(
            evidence.Evidence.Any(line => line.Contains("yield-started|rid=play-b", StringComparison.Ordinal)
                                          && line.Contains("handler=spot", StringComparison.Ordinal)),
            "YD-D3 target Spot handler marker missing.");
    }
}