using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdD2RemoteSpotYieldScenario
{
    public static async Task RunAsync(YieldDispatchScenarioContext context)
    {
        var ownerSpotRid = $"yield-remote-owner-{Guid.NewGuid():N}";
        var targetSpotRid = $"yield-remote-target-{Guid.NewGuid():N}";
        await context.Client.Request(new EnsureSpotReq(ownerSpotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();
        await context.Client.Request(new EnsureSpotReq(targetSpotRid))
            .PacketName("EnsureSpotReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();

        var requestId = $"YD-D2-{Guid.NewGuid():N}";
        await context.Client.Send(new RemoteSpotYieldCommand(requestId, targetSpotRid, 350))
            .PacketName("RemoteSpotYieldCommand")
            .Metadata(YieldDispatchNames.SpotRidMetadata, ownerSpotRid)
            .Async();
        await context.WaitForPlayEvidenceAsync(requestId, "remote-yield-released");
        await context.Client.Request(new ProbeReq(requestId, "remote-owner-probe"))
            .PacketName("ProbeReq")
            .Metadata(YieldDispatchNames.SpotRidMetadata, ownerSpotRid)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldDispatchReply>();
        var ownerEvidence = await context.WaitForPlayEvidenceAsync(requestId, "remote-yield-completed");
        ScenarioAssert.That(
            ownerEvidence.Evidence.Any(line =>
                line.Contains("remote-yield-resumed|rid=play-a", StringComparison.Ordinal)
                && line.Contains("targetNode=play-b", StringComparison.Ordinal)),
            "YD-D2 continuation did not return to the owner node.");
        var targetEvidence = await context.ReadPlayEvidenceAsync(requestId, "play-b");
        ScenarioAssert.ContainsInOrder(ownerEvidence.Evidence, requestId, [
            "remote-yield-started",
            "remote-yield-released",
            "probe-started",
            "probe-completed",
            "remote-yield-resumed",
            "remote-yield-completed"]);
        ScenarioAssert.That(
            targetEvidence.Evidence.Any(line =>
                line.Contains($"yield-started|rid=play-b|spot={targetSpotRid}|request={requestId}", StringComparison.Ordinal)),
            "YD-D2 target play-b marker missing.");
        ScenarioAssert.That(
            targetEvidence.Evidence.All(line => !line.Contains("remote-yield-resumed|rid=play-b", StringComparison.Ordinal)),
            "YD-D2 target node must not own the caller continuation.");
    }
}
