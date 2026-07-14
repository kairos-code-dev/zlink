using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class AsyncTerminatorProbe
{
    public static async Task<string> RunAsync(
        IZlinkStreamConnector client,
        string spotRid)
    {
        var requestId = $"probe-A2-{Guid.NewGuid():N}";
        client.Send(new AwaitMsg(requestId, 350, "corr-a2"))
            .PacketName("AwaitMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        await client.Request(new AwaitEvidenceWaitReq(requestId, "await-released"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        client.Send(new ProbeMsg(requestId, "await-probe"))
            .PacketName("ProbeMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "await-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "await-started",
            "await-released",
            "probe-started",
            "probe-completed",
            "await-resumed",
            "await-completed"
        ]);
        return requestId;
    }

}
