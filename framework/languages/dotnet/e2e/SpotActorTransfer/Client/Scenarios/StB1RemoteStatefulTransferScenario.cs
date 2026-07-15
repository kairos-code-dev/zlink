using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StB1RemoteStatefulTransferScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-remote-ok-{Guid.NewGuid():N}";
        var spotRid = $"spot-remote-ok-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 21);

        var join = await context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-B1", spotRid));
        ZlinkStreamAssert.Ensure(join.Accepted, "ST-B1 join was rejected.");

        var probe = await context.ProbeAsync(context.NodeB, actorId, new ProbeReq("ST-B1", "after-transfer"));
        ZlinkStreamAssert.Ensure(probe.NodeRid == "actor-b", $"ST-B1 probe expected actor-b, got {probe.NodeRid}.");
        ZlinkStreamAssert.Ensure(probe.StateVersion == 21, $"ST-B1 state version expected 21, got {probe.StateVersion}.");

        await context.WaitEvidenceAsync(context.NodeA, [
            $"transfer|{actorId}|transfer_out|21",
            $"transfer|{actorId}|leave|21",
            $"ST-B1|{actorId}|success_reply|{spotRid}"
        ]);
        await context.WaitEvidenceAsync(context.NodeB, [
            $"transfer|{actorId}|transfer_in|21",
            $"transfer|{actorId}|joined|{spotRid}:21",
            $"ST-B1|{actorId}|packet_handler|after-transfer"
        ]);
    }
}
