// Verifies ST-B1 Remote Stateful Transfer behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StB1RemoteStatefulTransferScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-remote-ok-{Guid.NewGuid():N}";
        var spotId = $"spot-remote-ok-{Guid.NewGuid():N}";
        var spot = await context.CreateSpotAsync(context.NodeB, spotId);
        var createdActor = await context.CreateActorAsync(
            context.NodeA,
            actorId,
            SpotActorTransferNames.ActorTypeStateful,
            21);
        var source = context.NodeForRid(createdActor.NodeRid);
        var target = context.NodeForRid(spot.NodeRid);

        var join = await context.JoinAsync(source, actorId, new JoinTargetReq("ST-B1", spotId));
        ZlinkStreamAssert.Ensure(join.Accepted, "ST-B1 join was rejected.");

        await context.WaitEvidenceAsync(target, [
            $"transfer|{actorId}|transfer_in|21",
            $"transfer|{actorId}|joined|{spotId}:21",
            $"ST-B1|{actorId}|success_reply|{spotId}"
        ]);
        _ = await context.WaitActorOwnerAsync(
            target,
            actorId,
            spot.NodeRid);
        var probe = await context.ProbeAsync(target, actorId, new ProbeReq("ST-B1", "after-transfer"));
        ZlinkStreamAssert.Ensure(probe.NodeRid == spot.NodeRid, $"ST-B1 probe expected {spot.NodeRid}, got {probe.NodeRid}.");
        ZlinkStreamAssert.Ensure(probe.StateVersion == 21, $"ST-B1 state version expected 21, got {probe.StateVersion}.");

        await context.WaitEvidenceAsync(source, [
            $"transfer|{actorId}|transfer_out|21",
            $"transfer|{actorId}|leave|21"
        ]);
        await context.WaitEvidenceAsync(target, [
            $"ST-B1|{actorId}|packet_handler|after-transfer"
        ]);
    }
}
