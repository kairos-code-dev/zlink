using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdA3ContinuationContextScenario
{
    public static async Task<string> RunAsync(YieldDispatchScenarioContext context, string spotRid)
    {
        var requestId = $"YD-A3-{Guid.NewGuid():N}";
        await context.Client.Send(new YieldCommand(requestId, 50, "corr-a3"))
            .PacketName("YieldCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await context.WaitForPlayEvidenceAsync(requestId, "yield-completed");
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "yield-started",
            "yield-released",
            "yield-resumed",
            "yield-completed"]);
        return requestId;
    }
}
