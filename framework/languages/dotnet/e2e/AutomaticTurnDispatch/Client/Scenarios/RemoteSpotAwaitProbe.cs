using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class RemoteSpotAwaitProbe
{
    public static async Task RunAsync(IZlinkStreamConnector client)
    {
        var ownerSpotRid = $"await-remote-owner-{Guid.NewGuid():N}";
        var targetSpotRid = $"await-remote-target-{Guid.NewGuid():N}";
        await client.Request(new EnsureSpotReq(ownerSpotRid))
            .PacketName("EnsureSpotReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotRes>();
        await client.Request(new EnsureSpotReq(targetSpotRid))
            .PacketName("EnsureSpotReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<EnsureSpotRes>();

        var requestId = $"probe-D2-{Guid.NewGuid():N}";
        var reply = await client.Request(new RemoteSpotAwaitReq(requestId, targetSpotRid, 350))
            .PacketName("RemoteSpotAwaitReq")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, ownerSpotRid)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AutomaticTurnDispatchRes>();
        ZlinkStreamAssert.Ensure(reply.ScenarioId == "probe-D2", "probe-D2 reply scenario mismatch.");
        ZlinkStreamAssert.Ensure(reply.NodeRid == "play-a", "probe-D2 caller continuation node mismatch.");
        var ownerEvidence = await client.Request(new AwaitEvidenceWaitReq(requestId, "remote-await-completed"))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        ZlinkStreamAssert.Ensure(
            ownerEvidence.Evidence.Any(line =>
                line.Contains("remote-await-resumed|rid=play-a", StringComparison.Ordinal)
                && line.Contains("targetNode=play-b", StringComparison.Ordinal)),
            "probe-D2 continuation did not return to the owner node.");
        var targetEvidence = await client.Request(new AwaitEvidenceReq(requestId))
            .PacketName("AwaitEvidenceReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-b")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        EvidenceOrder.ContainsInOrder(ownerEvidence.Evidence, requestId, [
            "remote-await-started",
            "remote-await-released",
            "remote-await-resumed",
            "remote-await-completed"
        ]);
        ZlinkStreamAssert.Ensure(
            targetEvidence.Evidence.Any(line =>
                line.Contains($"await-started|rid=play-b|spot={targetSpotRid}|request={requestId}",
                    StringComparison.Ordinal)),
            "probe-D2 target play-b marker missing.");
        ZlinkStreamAssert.Ensure(
            targetEvidence.Evidence.All(line =>
                !line.Contains("remote-await-resumed|rid=play-b", StringComparison.Ordinal)),
            "probe-D2 target node must not own the caller continuation.");
    }
}
