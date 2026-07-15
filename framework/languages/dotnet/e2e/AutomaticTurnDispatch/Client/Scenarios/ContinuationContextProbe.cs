// Captures the execution context observed when a continuation resumes.
using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class ContinuationContextProbe
{
    public static async Task<string> RunAsync(IZlinkStreamConnector client, string spotRid)
    {
        var requestId = $"probe-A3-{Guid.NewGuid():N}";
        client.Send(new AwaitMsg(requestId, 50, "corr-a3"))
            .PacketName("AwaitMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "await-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        EvidenceOrder.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "await-started",
            "await-released",
            "await-resumed",
            "await-completed"
        ]);
        return requestId;
    }
}
