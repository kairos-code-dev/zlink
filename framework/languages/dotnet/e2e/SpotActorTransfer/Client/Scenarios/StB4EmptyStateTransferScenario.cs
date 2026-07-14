using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StB4EmptyStateTransferScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-empty-state-{Guid.NewGuid():N}";
        var spotRid = $"spot-empty-state-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeEmptyState, 41);

        var join = await context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-B4", spotRid));
        SpotActorTransferScenarioContext.Require(join.Accepted, "ST-B4 join was rejected.");

        var probe = await context.ProbeAsync(context.NodeB, actorId, new ProbeReq("ST-B4", "after-empty-state-transfer"));
        SpotActorTransferScenarioContext.Require(probe.NodeRid == "actor-b", $"ST-B4 probe expected actor-b, got {probe.NodeRid}.");
        SpotActorTransferScenarioContext.Require(probe.StateVersion == 41, $"ST-B4 loaded target state expected 41, got {probe.StateVersion}.");

        await context.WaitEvidenceAsync(context.NodeA, [
            $"transfer|{actorId}|transfer_out_empty|custom-adapter",
            $"transfer|{actorId}|leave|41"
        ]);
        await context.WaitEvidenceAsync(context.NodeB, [
            $"transfer|{actorId}|transfer_in_empty|custom-adapter",
            $"transfer|{actorId}|joined|{spotRid}:0",
            $"transfer|{actorId}|domain_state_loaded|{actorId}",
            $"ST-B4|{actorId}|packet_handler|after-empty-state-transfer"
        ]);
    }
}
