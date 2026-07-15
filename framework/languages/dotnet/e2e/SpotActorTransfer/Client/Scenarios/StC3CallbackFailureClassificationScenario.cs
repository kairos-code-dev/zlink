using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StC3CallbackFailureClassificationScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        await RunTransferOutFailureAsync(context);
        await RunSourceLeaveFailureAsync(context);
        await RunTransferInFailureAsync(context);
        await RunJoinedFailureAsync(context);
    }

    private static async Task RunTransferOutFailureAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-fail-transfer-out-{Guid.NewGuid():N}";
        var spotRid = $"spot-fail-transfer-out-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeFailTransferOut, 71);

        var response = await context.JoinRawAsync(context.NodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
        ZlinkStreamAssert.Ensure(!response.Accepted, "ST-C3 transfer-out failure should not return accepted.");
        var sourceEvidence = await context.WaitEvidenceAsync(context.NodeA, [
            $"ST-C3|{actorId}|transfer_out_failed|71",
            $"ST-C3|{actorId}|join_failed|"
        ]);
        SpotActorTransferScenarioContext.RequireNoContains(sourceEvidence, $"transfer|{actorId}|leave|71", "ST-C3 transfer-out failure should not leave source.");
        var targetEvidence = await context.GetEvidenceAsync(context.NodeB);
        SpotActorTransferScenarioContext.RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C3 transfer-out failure should not join target.");
    }

    private static async Task RunSourceLeaveFailureAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-fail-leave-{Guid.NewGuid():N}";
        var spotRid = $"spot-fail-leave-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeFailLeave, 72);

        var response = await context.JoinRawAsync(context.NodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
        ZlinkStreamAssert.Ensure(!response.Accepted, "ST-C3 source leave failure should not return accepted.");
        await context.WaitEvidenceAsync(context.NodeA, [
            $"transfer|{actorId}|transfer_out|72",
            $"ST-C3|{actorId}|leave_failed|72",
            $"ST-C3|{actorId}|join_failed|"
        ]);
        var targetEvidence = await context.GetEvidenceAsync(context.NodeB);
        SpotActorTransferScenarioContext.RequireNoContains(targetEvidence, $"transfer|{actorId}|transfer_in|72", "ST-C3 source leave failure should not transfer in target.");
        SpotActorTransferScenarioContext.RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C3 source leave failure should not join target.");
    }

    private static async Task RunTransferInFailureAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-fail-transfer-in-{Guid.NewGuid():N}";
        var spotRid = $"spot-fail-transfer-in-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeFailTransferIn, 73);

        var response = await context.JoinRawAsync(context.NodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
        ZlinkStreamAssert.Ensure(!response.Accepted, "ST-C3 transfer-in failure should not return accepted.");
        await context.WaitEvidenceAsync(context.NodeB, [
            $"ST-C3|{actorId}|transfer_in_failed|73"
        ]);
        await context.WaitEvidenceAsync(context.NodeA, [
            $"transfer|{actorId}|transfer_out|73",
            $"transfer|{actorId}|leave|73",
            $"ST-C3|{actorId}|join_failed|"
        ]);
        var targetEvidence = await context.GetEvidenceAsync(context.NodeB);
        SpotActorTransferScenarioContext.RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C3 transfer-in failure should not join target.");
    }

    private static async Task RunJoinedFailureAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-fail-joined-{Guid.NewGuid():N}";
        var spotRid = $"spot-fail-joined-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid, "fail-joined");
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 74);

        var response = await context.JoinRawAsync(context.NodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
        ZlinkStreamAssert.Ensure(!response.Accepted, "ST-C3 joined failure should not return accepted.");
        await context.WaitEvidenceAsync(context.NodeB, [
            $"ST-C3|{actorId}|joined_failed|{spotRid}"
        ]);
        await context.WaitEvidenceAsync(context.NodeA, [
            $"transfer|{actorId}|transfer_out|74",
            $"transfer|{actorId}|leave|74",
            $"ST-C3|{actorId}|join_failed|"
        ]);
        var targetEvidence = await context.GetEvidenceAsync(context.NodeB);
        SpotActorTransferScenarioContext.RequireNoContains(targetEvidence, $"ST-C3|{actorId}|packet_handler|after-joined-failure", "ST-C3 joined failure should not dispatch as joined.");
    }
}
