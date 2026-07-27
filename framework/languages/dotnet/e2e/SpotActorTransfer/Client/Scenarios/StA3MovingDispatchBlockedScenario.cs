// Verifies ST-A3 Moving Dispatch Blocked behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StA3MovingDispatchBlockedScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-local-moving-{Guid.NewGuid():N}";
        var spotId = $"spot-local-moving-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeA, spotId, "delay-joined");
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 13);

        var joinTask = context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-A3", spotId));
        var waitingEvidence = await context.WaitEvidenceAsync(context.NodeA, [
            $"ST-A3|{actorId}|admission|spot={spotId}",
            $"transfer|{actorId}|leave|13",
            $"ST-A3|{actorId}|joined_wait|{spotId}"
        ]);
        ZlinkStreamAssert.Ensure(
            !waitingEvidence.Any(item => SpotActorTransferScenarioContext.EvidenceText(item)
                .Contains($"ST-A3|{actorId}|packet_handler|during-joined-wait", StringComparison.Ordinal)),
            "ST-A3 packet should not run before OnJoinedActorAsync is released.");

        var blockedProbe = context.ProbeAsync(context.NodeA, actorId, new ProbeReq("ST-A3", "during-joined-wait"));
        var submittedEvidence = await context.WaitEvidenceAsync(context.NodeA, [
            $"ST-A3|{actorId}|probe_submitted|during-joined-wait"
        ]);
        ZlinkStreamAssert.Ensure(
            !submittedEvidence.Any(item => SpotActorTransferScenarioContext.EvidenceText(item)
                .Contains($"ST-A3|{actorId}|packet_handler|during-joined-wait", StringComparison.Ordinal)),
            "ST-A3 actor packet completed while OnJoinedActorAsync was still blocked.");

        var release = await context.ReleaseJoinedGateAsync(context.NodeA, spotId);
        ZlinkStreamAssert.Ensure(release.Released, "ST-A3 joined gate was already released before the scenario released it.");

        var join = await joinTask;
        ZlinkStreamAssert.Ensure(join.Accepted, "ST-A3 join was rejected.");
        var probe = await blockedProbe.WaitAsync(TimeSpan.FromSeconds(10));
        ZlinkStreamAssert.Ensure(probe.SpotId == spotId, "ST-A3 blocked packet did not resume on the target spot.");

        await context.WaitEvidenceAsync(context.NodeA, [
            $"ST-A3|{actorId}|joined_released|{spotId}",
            $"transfer|{actorId}|joined|{spotId}:13",
            $"ST-A3|{actorId}|packet_handler|during-joined-wait",
            $"ST-A3|{actorId}|success_reply|{spotId}"
        ]);
    }
}
