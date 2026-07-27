// Verifies ST-A2 Local Reject behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StA2LocalRejectScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-local-reject-{Guid.NewGuid():N}";
        var spotId = $"spot-local-reject-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeA, spotId, "reject");
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 12);

        var join = await context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-A2", spotId, "reject"));
        ZlinkStreamAssert.Ensure(!join.Accepted, "ST-A2 join should have been rejected.");

        var evidence = await context.WaitEvidenceAsync(context.NodeA, [
            $"ST-A2|{actorId}|admission|spot={spotId}"
        ]);
        ZlinkStreamAssert.Ensure(
            !evidence.Any(item => SpotActorTransferScenarioContext.EvidenceText(item)
                .Contains($"transfer|{actorId}|joined|{spotId}", StringComparison.Ordinal)),
            "ST-A2 joined side effect should not exist.");
    }
}
