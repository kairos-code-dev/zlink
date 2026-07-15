using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class WorkerAwaitProbe
{
    public static async Task<string> RunAsync(IZlinkStreamConnector client, string spotRid)
    {
        var requestId = $"probe-A4-{Guid.NewGuid():N}";
        client.Send(new WorkerAwaitMsg(requestId, 350))
            .PacketName("WorkerAwaitMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        await client.Request(new AwaitEvidenceWaitReq(requestId, "worker-await-released"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        client.Send(new ProbeMsg(requestId, "worker-probe"))
            .PacketName("ProbeMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "worker-await-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        EvidenceOrder.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "worker-await-started",
            "worker-await-released",
            "probe-started",
            "probe-completed",
            "worker-await-resumed",
            "worker-await-completed"
        ]);
        return requestId;
    }
}
