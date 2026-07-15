// Verifies a timer callback cannot reenter before its current callback completes.
using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class TimerReentryProbe
{
    public static async Task<string> RunAsync(IZlinkStreamConnector client, string spotRid)
    {
        var requestId = $"probe-C2-{Guid.NewGuid():N}";
        client.Send(new TimerStartMsg(requestId, $"{requestId}-same", "await-then-next", 50, 350))
            .PacketName("TimerStartMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "timer-next-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        EvidenceOrder.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "timer-await-started",
            "timer-await-released",
            "timer-await-resumed",
            "timer-await-completed",
            "timer-next-started",
            "timer-next-completed"
        ]);
        client.Send(new TimerStopMsg(requestId))
            .PacketName("TimerStopMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        return requestId;
    }
}
