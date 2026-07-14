using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class TimerIsolationProbe
{
    public static async Task<(string SpotRid, string RequestId)> RunAsync(IZlinkStreamConnector client)
    {
        var spotRid = $"await-timer-{Guid.NewGuid():N}";
        var spot = await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotRes>();
        ScenarioAssert.That(spot.SpotRid == spotRid, "probe-C timer spot creation mismatch.");

        var requestId = $"probe-C1-{Guid.NewGuid():N}";
        client.Send(new TimerStartMsg(requestId, $"{requestId}-await", "await-on-first", 50, 350))
            .PacketName("TimerStartMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        await client.Request(new AwaitEvidenceWaitReq(requestId, "timer-await-released"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        client.Send(new TimerStartMsg(requestId, $"{requestId}-fast", "fast", 50, 0))
            .PacketName("TimerStartMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        await client.Request(new AwaitEvidenceWaitReq(requestId, "timer-fast-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        var evidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "timer-await-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "timer-await-started",
            "timer-await-released",
            "timer-fast-started",
            "timer-fast-completed",
            "timer-await-resumed",
            "timer-await-completed"
        ]);
        client.Send(new TimerStopMsg(requestId))
            .PacketName("TimerStopMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        return (spotRid, requestId);
    }
}
