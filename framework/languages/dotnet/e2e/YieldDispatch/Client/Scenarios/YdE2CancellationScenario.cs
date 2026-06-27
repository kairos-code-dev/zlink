using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdE2CancellationScenario
{
    public static async Task RunAsync(YieldDispatchScenarioContext context)
    {
        var spotRid = $"yield-cancel-{Guid.NewGuid():N}";
        var spot = await context.Client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();
        ScenarioAssert.That(spot.SpotRid == spotRid, "YD-E2 spot creation mismatch.");

        var requestId = $"YD-E2-{Guid.NewGuid():N}";
        await context.Client.Send(new YieldCancelCommand(requestId, DelayMs: 800, CancelAfterMs: 100))
            .PacketName("YieldCancelCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await context.WaitForPlayEvidenceAsync(requestId, "cancel-yield-completed");
        await context.Client.Send(new ProbeCommand(requestId, "cancel-probe"))
            .PacketName("ProbeCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await context.WaitForPlayEvidenceAsync(requestId, "probe-completed");
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "cancel-yield-started",
            "cancel-yield-released",
            "cancel-yield-completed",
            "probe-started",
            "probe-completed"]);
    }
}
