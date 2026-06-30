using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdD2RemoteSpotYieldScenario
{
    public static async Task RunAsync(IZlinkStreamConnector client)
    {
        var ownerSpotRid = $"yield-remote-owner-{Guid.NewGuid():N}";
        var targetSpotRid = $"yield-remote-target-{Guid.NewGuid():N}";
        await client.Request(new EnsureSpotReq(ownerSpotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();
        await client.Request(new EnsureSpotReq(targetSpotRid))
            .PacketName("EnsureSpotReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotReply>();

        var requestId = $"YD-D2-{Guid.NewGuid():N}";
        var reply = await client.Request(new RemoteSpotYieldReq(requestId, targetSpotRid, 350))
            .PacketName("RemoteSpotYieldReq")
            .Metadata(YieldDispatchNames.SpotRidMetadata, ownerSpotRid)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldDispatchReply>();
        ScenarioAssert.That(reply.ScenarioId == "YD-D2", "YD-D2 reply scenario mismatch.");
        ScenarioAssert.That(reply.NodeRid == "play-a", "YD-D2 caller continuation node mismatch.");
        var ownerEvidence = await client.Request(new YieldEvidenceWaitReq(requestId, "remote-yield-completed"))
            .PacketName("YieldEvidenceWaitReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.That(
            ownerEvidence.Evidence.Any(line =>
                line.Contains("remote-yield-resumed|rid=play-a", StringComparison.Ordinal)
                && line.Contains("targetNode=play-b", StringComparison.Ordinal)),
            "YD-D2 continuation did not return to the owner node.");
        var targetEvidence = await client.Request(new YieldEvidenceReq(requestId))
            .PacketName("YieldEvidenceReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsInOrder(ownerEvidence.Evidence, requestId, [
            "remote-yield-started",
            "remote-yield-released",
            "remote-yield-resumed",
            "remote-yield-completed"
        ]);
        ScenarioAssert.That(
            targetEvidence.Evidence.Any(line =>
                line.Contains($"yield-started|rid=play-b|spot={targetSpotRid}|request={requestId}",
                    StringComparison.Ordinal)),
            "YD-D2 target play-b marker missing.");
        ScenarioAssert.That(
            targetEvidence.Evidence.All(line =>
                !line.Contains("remote-yield-resumed|rid=play-b", StringComparison.Ordinal)),
            "YD-D2 target node must not own the caller continuation.");
    }
}