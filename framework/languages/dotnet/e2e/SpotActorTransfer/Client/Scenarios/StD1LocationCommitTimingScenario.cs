using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StD1LocationCommitTimingScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        await RunLocalLocationCommitTimingAsync(context);
        await RunRemoteLocationCommitTimingAsync(context);
    }

    private static async Task RunLocalLocationCommitTimingAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-location-local-{Guid.NewGuid():N}";
        var spotRid = $"spot-location-local-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeA, spotRid, "delay-joined");
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 51);
        var before = await context.GetActorRefAsync(context.NodeA, actorId);

        var joinTask = context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-D1", spotRid));
        var waitingEvidence = await context.WaitEvidenceAsync(context.NodeA, [
            $"ST-D1|{actorId}|admission|spot={spotRid}",
            $"ST-D1|{actorId}|joined_wait|{spotRid}"
        ]);
        SpotActorTransferScenarioContext.RequireNoContains(
            waitingEvidence,
            $"ST-D1|{actorId}|success_reply|{spotRid}",
            "ST-D1 local join returned success before OnJoinedActorAsync completed.");
        var during = await context.GetActorRefAsync(context.NodeA, actorId);
        SpotActorTransferScenarioContext.Require(
            during.Generation == before.Generation,
            $"ST-D1 local actor generation changed before joined completed. before={before.Generation}, during={during.Generation}");

        await context.ReleaseJoinedGateAsync(context.NodeA, spotRid);
        var join = await joinTask;
        SpotActorTransferScenarioContext.Require(join.Accepted, "ST-D1 local join was rejected.");
        var after = await context.GetActorRefAsync(context.NodeA, actorId);
        SpotActorTransferScenarioContext.Require(after.Generation >= before.Generation, "ST-D1 local actor generation regressed after commit.");

        await context.WaitEvidenceAsync(context.NodeA, [
            $"ST-D1|{actorId}|joined_released|{spotRid}",
            $"transfer|{actorId}|joined|{spotRid}:51",
            $"ST-D1|{actorId}|success_reply|{spotRid}"
        ]);
    }

    private static async Task RunRemoteLocationCommitTimingAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-location-remote-{Guid.NewGuid():N}";
        var spotRid = $"spot-location-remote-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid, "delay-joined");
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 52);

        var joinTask = context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-D1", spotRid));
        await context.WaitEvidenceAsync(context.NodeB, [
            $"ST-D1|{actorId}|admission|spot={spotRid}",
            $"ST-D1|{actorId}|joined_wait|{spotRid}"
        ]);
        var sourceDuring = await context.GetActorRefAsync(context.NodeA, actorId);
        SpotActorTransferScenarioContext.Require(
            sourceDuring.NodeRid == "actor-a",
            $"ST-D1 remote source ref moved before target joined completed. got={sourceDuring.NodeRid}");

        await context.ReleaseJoinedGateAsync(context.NodeB, spotRid);
        var join = await joinTask;
        SpotActorTransferScenarioContext.Require(join.Accepted, "ST-D1 remote join was rejected.");
        var targetAfter = await context.GetActorRefAsync(context.NodeB, actorId);
        SpotActorTransferScenarioContext.Require(
            targetAfter.NodeRid == "actor-b",
            $"ST-D1 remote target ref was not committed after joined completed. got={targetAfter.NodeRid}");

        await context.WaitEvidenceAsync(context.NodeA, [
            $"transfer|{actorId}|leave|52",
            $"ST-D1|{actorId}|success_reply|{spotRid}"
        ]);
        await context.WaitEvidenceAsync(context.NodeB, [
            $"ST-D1|{actorId}|joined_released|{spotRid}",
            $"transfer|{actorId}|joined|{spotRid}:52"
        ]);
    }
}
