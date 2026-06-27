using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdA4WorkerYieldScenario
{
    public static async Task<string> RunAsync(YieldDispatchScenarioContext context, string spotRid)
    {
        var requestId = $"YD-A4-{Guid.NewGuid():N}";
        await context.Client.Send(new WorkerYieldCommand(requestId, 350))
            .PacketName("WorkerYieldCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        await context.WaitForPlayEvidenceAsync(requestId, "worker-yield-released");
        await context.Client.Send(new ProbeCommand(requestId, "worker-probe"))
            .PacketName("ProbeCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, spotRid)
            .Async();
        var evidence = await context.WaitForPlayEvidenceAsync(requestId, "worker-yield-completed");
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "worker-yield-started",
            "worker-yield-released",
            "probe-started",
            "probe-completed",
            "worker-yield-resumed",
            "worker-yield-completed"]);
        return requestId;
    }
}
