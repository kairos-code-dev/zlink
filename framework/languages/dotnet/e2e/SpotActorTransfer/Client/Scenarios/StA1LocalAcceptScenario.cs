using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StA1LocalAcceptScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-local-ok-{Guid.NewGuid():N}";
        var spotRid = $"spot-local-ok-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeA, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 11);

        var join = await context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-A1", spotRid));
        SpotActorTransferScenarioContext.Require(join.Accepted, "ST-A1 join was rejected.");

        var probe = await context.ProbeAsync(context.NodeA, actorId, new ProbeReq("ST-A1", "after-joined"));
        SpotActorTransferScenarioContext.Require(probe.NodeRid == "actor-a", $"ST-A1 probe expected actor-a, got {probe.NodeRid}.");
        SpotActorTransferScenarioContext.Require(probe.SpotRid == spotRid, "ST-A1 probe did not reach target spot.");

        var evidence = await context.WaitEvidenceAsync(context.NodeA, [
            $"ST-A1|{actorId}|admission|spot={spotRid}",
            $"transfer|{actorId}|leave|11",
            $"transfer|{actorId}|joined|{spotRid}:11",
            $"ST-A1|{actorId}|success_reply|{spotRid}",
            $"ST-A1|{actorId}|packet_handler|after-joined"
        ]);
        SpotActorTransferScenarioContext.RequireContains(evidence, $"ST-A1|{actorId}|packet_handler|after-joined", "ST-A1 packet evidence missing.");
    }
}
