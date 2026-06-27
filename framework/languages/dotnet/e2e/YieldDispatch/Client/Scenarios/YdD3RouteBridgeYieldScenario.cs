using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdD3RouteBridgeYieldScenario
{
    public static async Task RunAsync(YieldDispatchScenarioContext context)
    {
        var requestId = $"YD-D3-{Guid.NewGuid():N}";
        var spotRid = $"yield-route-bridge-{Guid.NewGuid():N}";
        await context.Client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();
        await context.Client.Send(new YieldCommand(requestId, 250, "route-bridge"))
            .PacketName("YieldCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await Task.Delay(75);
        await context.Client.Send(new ProbeCommand(requestId, "route-bridge-probe"))
            .PacketName("ProbeCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await context.WaitForPlayEvidenceAsync(requestId, "yield-completed", "play-b");
        ScenarioAssert.ContainsInOrder(evidence.Evidence, requestId, [
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"]);
        ScenarioAssert.That(
            evidence.Evidence.Any(line => line.Contains("yield-started|rid=play-b", StringComparison.Ordinal)
                && line.Contains("handler=spot", StringComparison.Ordinal)),
            "YD-D3 target Spot handler marker missing.");
    }
}
