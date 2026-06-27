using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdA2YieldTerminatorScenario
{
    public static async Task<string> RunAsync(YieldDispatchScenarioContext context, string spotRid)
    {
        var requestId = $"YD-A2-{Guid.NewGuid():N}";
        await context.Client.Send(new YieldCommand(requestId, 350, "corr-a2"))
            .PacketName("YieldCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await context.WaitForPlayEvidenceAsync(requestId, "yield-released");
        await context.Client.Send(new ProbeCommand(requestId, "yield-probe"))
            .PacketName("ProbeCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await context.WaitForPlayEvidenceAsync(requestId, "yield-completed");
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"]);
        return requestId;
    }
}
