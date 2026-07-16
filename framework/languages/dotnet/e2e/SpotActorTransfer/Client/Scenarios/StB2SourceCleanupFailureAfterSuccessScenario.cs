// Verifies ST-B2 Source Cleanup Failure After Success behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StB2SourceCleanupFailureAfterSuccessScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-cleanup-after-success-{Guid.NewGuid():N}";
        var spotRid = $"spot-cleanup-after-success-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 22);
        await context.ArmCleanupGateAsync(context.NodeA, actorId, "ST-B2");

        var join = await context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-B2", spotRid));
        ZlinkStreamAssert.Ensure(join.Accepted, "ST-B2 join was rejected.");
        await context.AllowCleanupAttemptAsync(context.NodeA, actorId, "ST-B2");
        await context.WaitEvidenceAsync(context.NodeA, [
            $"ST-B2|{actorId}|success_reply|{spotRid}",
            $"ST-B2|{actorId}|source_cleanup_attempt|"
        ]);
        var beforeShutdown = await context.ProbeAsync(context.NodeB, actorId, new ProbeReq("ST-B2", "before-source-cleanup-loss"));
        ZlinkStreamAssert.Ensure(beforeShutdown.NodeRid == "actor-b", $"ST-B2 probe expected actor-b, got {beforeShutdown.NodeRid}.");

        await context.CrashNodeAAndWaitUnavailableAsync();

        var afterShutdown = await context.ProbeAsync(context.NodeB, actorId, new ProbeReq("ST-B2", "after-source-cleanup-loss"));
        ZlinkStreamAssert.Ensure(afterShutdown.NodeRid == "actor-b", $"ST-B2 target ownership was lost after source shutdown: {afterShutdown.NodeRid}.");
        ZlinkStreamAssert.Ensure(afterShutdown.StateVersion == 22, $"ST-B2 state changed after source cleanup loss: {afterShutdown.StateVersion}.");
        await context.WaitEvidenceAsync(context.NodeB, [
            $"transfer|{actorId}|joined|{spotRid}:22",
            $"ST-B2|{actorId}|packet_handler|after-source-cleanup-loss"
        ]);
    }
}
