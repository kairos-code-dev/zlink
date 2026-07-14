using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class RequestTimeoutProbe
{
    public static async Task RunAsync(IZlinkStreamConnector client)
    {
        var spotRid = $"await-timeout-{Guid.NewGuid():N}";
        var spot = await client.Request(new EnsureSpotReq(spotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotRes>();
        ScenarioAssert.That(spot.SpotRid == spotRid, "probe-E1 spot creation mismatch.");

        var requestId = $"probe-E1-{Guid.NewGuid():N}";
        client.Send(new AwaitTimeoutMsg(requestId, 700, 100))
            .PacketName("AwaitTimeoutMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        await client.Request(new AwaitEvidenceWaitReq(requestId, "timeout-await-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        client.Send(new ProbeMsg(requestId, "timeout-probe"))
            .PacketName("ProbeMsg")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
        var evidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "probe-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        ScenarioAssert.That(
            evidence.Evidence.Any(line => line.Contains($"request={requestId}", StringComparison.Ordinal)
                                          && line.Contains("timeout-await-completed", StringComparison.Ordinal)),
            "probe-E1 timeout marker missing.");
        ScenarioAssert.That(
            evidence.Evidence.Any(line => line.Contains($"request={requestId}", StringComparison.Ordinal)
                                          && line.Contains("probe-completed", StringComparison.Ordinal)
                                          && line.Contains("timeout-probe", StringComparison.Ordinal)),
            "probe-E1 post-timeout probe marker missing.");
    }
}
