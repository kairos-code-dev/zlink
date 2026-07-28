// Verifies ST-A1 Local Accept behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StA1LocalAcceptScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        await context.ResetRelocationBlobMeasurementsAsync(
            context.NodeA,
            context.NodeB);
        var actorId = $"actor-local-ok-{Guid.NewGuid():N}";
        var spotId = $"spot-local-ok-{Guid.NewGuid():N}";
        var actor = await context.CreateActorAsync(
            context.NodeA,
            actorId,
            SpotActorTransferNames.ActorTypeStateful,
            11);
        var owner = context.NodeForRid(actor.NodeRid);
        await context.CreateSpotAsync(owner, spotId);

        var join = await context.JoinAsync(owner, actorId, new JoinTargetReq("ST-A1", spotId));
        ZlinkStreamAssert.Ensure(join.Accepted, "ST-A1 join was rejected.");

        await context.WaitEvidenceAsync(owner, [
            $"ST-A1|{actorId}|success_reply|{spotId}"
        ]);

        var probe = await context.ProbeAsync(owner, actorId, new ProbeReq("ST-A1", "after-joined"));
        ZlinkStreamAssert.Ensure(probe.NodeRid == actor.NodeRid,
            $"ST-A1 probe expected {actor.NodeRid}, got {probe.NodeRid}.");
        ZlinkStreamAssert.Ensure(probe.SpotId == spotId, "ST-A1 probe did not reach target spot.");

        var evidence = await context.WaitEvidenceAsync(owner, [
            $"ST-A1|{actorId}|admission|spot={spotId}",
            $"runtime|{actorId}|authority_committed|{spotId}",
            $"transfer|{actorId}|leave|11",
            $"transfer|{actorId}|joined|{spotId}:11",
            $"ST-A1|{actorId}|success_reply|{spotId}",
            $"ST-A1|{actorId}|packet_handler|after-joined"
        ]);
        ZlinkStreamAssert.Ensure(
            evidence.Any(item => SpotActorTransferScenarioContext.EvidenceText(item)
                .Contains($"ST-A1|{actorId}|packet_handler|after-joined", StringComparison.Ordinal)),
            "ST-A1 packet evidence missing.");

        var actorEvidence = evidence
            .Where(item => item.ActorId == actorId)
            .ToArray();
        var admission = Array.FindIndex(actorEvidence, item =>
            item.Scenario == "ST-A1"
            && item.Kind == "admission"
            && item.Value == $"spot={spotId}");
        var authorityCommitted = Array.FindIndex(actorEvidence, item =>
            item.Scenario == "runtime"
            && item.Kind == "authority_committed"
            && item.Value == spotId);
        var leave = Array.FindIndex(actorEvidence, item =>
            item.Kind == "transfer"
            && item.Value == "leave|11");
        var joined = Array.FindIndex(actorEvidence, item =>
            item.Kind == "transfer"
            && item.Value == $"joined|{spotId}:11");
        var successReply = Array.FindIndex(actorEvidence, item =>
            item.Scenario == "ST-A1"
            && item.Kind == "success_reply"
            && item.Value == spotId);
        ZlinkStreamAssert.Ensure(
            admission >= 0
            && admission < authorityCommitted
            && authorityCommitted < leave
            && leave < joined
            && joined < successReply,
            "ST-A1 order must be admission -> authority_committed -> leave "
            + "-> joined -> success_reply.");

        var relocationArtifacts = (await context
                .GetRelocationBlobMeasurementsAsync(context.NodeA))
            .Concat(await context
                .GetRelocationBlobMeasurementsAsync(context.NodeB))
            .ToArray();
        ZlinkStreamAssert.Ensure(
            relocationArtifacts.Length == 0,
            "ST-A1 same-node Join must not create Relocation Store artifacts.");
    }
}
