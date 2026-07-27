// Verifies ST-F5 Message Follow route removal.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StF5MessageFollowRouteRemovalScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        const string scenario = "ST-F5";
        var actorId = $"actor-message-follow-chain-{Guid.NewGuid():N}";
        var firstSpot = $"spot-message-follow-chain-one-{Guid.NewGuid():N}";
        var finalSpot = $"spot-message-follow-chain-final-{Guid.NewGuid():N}";
        var created = await context.CreateActorAsync(
            context.NodeA,
            actorId,
            SpotActorTransferNames.ActorTypeStateful,
            105);
        var source = context.NodeForRid(created.NodeRid);
        var (firstTarget, _) = context.OtherActorNode(created.NodeRid);
        var finalTarget = context.ThirdActorNode(source, firstTarget);
        await context.CreateSpotAsync(firstTarget, firstSpot);
        await context.CreateSpotAsync(finalTarget, finalSpot);

        var chainOperation = Guid.NewGuid().ToString("N");
        var expiredOperation = Guid.NewGuid().ToString("N");
        await context.ArmTransportDeliveryAsync(
            source, chainOperation, actorId, "OneWay");
        await context.ArmTransportDeliveryAsync(
            source, expiredOperation, actorId, "Request");
        var chained = context.SendFromNodeAsync(
            source,
            actorId,
            new HandoffPacket(scenario, "chain-to-final"),
            chainOperation);
        var expired = context.ProbeFromNodeAsync(
            source,
            actorId,
            new ProbeReq(scenario, "after-route-removal"),
            TimeSpan.FromSeconds(15),
            expiredOperation);
        await context.WaitTransportDeliveryAsync(source, chainOperation);
        await context.WaitTransportDeliveryAsync(source, expiredOperation);

        ZlinkStreamAssert.Ensure((await context.JoinAsync(
                source,
                actorId,
                new JoinTargetReq(scenario, firstSpot))).Accepted,
            "ST-F5 first transfer was rejected.");
        ZlinkStreamAssert.Ensure((await context.JoinAsync(
                firstTarget,
                actorId,
                new JoinTargetReq(scenario, finalSpot))).Accepted,
            "ST-F5 chained transfer was rejected.");

        await context.ReleaseTransportDeliveryAsync(source, chainOperation);
        await chained;
        await context.WaitEvidenceAsync(
            finalTarget,
            [$"{scenario}|{actorId}|handoff_packet|chain-to-final"]);
        await context.WaitRuntimeEvidenceAsync(
            source,
            $"message_follow_relay actor={actorId}");
        await context.WaitRuntimeEvidenceAsync(
            firstTarget,
            $"message_follow_relay actor={actorId}");

        await context.WaitRuntimeEvidenceAsync(source,
            $"message_follow_route_removed actor={actorId} entries=0");
        await context.WaitRuntimeEvidenceAsync(firstTarget,
            $"message_follow_route_removed actor={actorId} entries=0");
        await context.ReleaseTransportDeliveryAsync(
            source, expiredOperation);
        var stale = await expired;
        ZlinkStreamAssert.Ensure(!stale.Succeeded && stale.ErrorKind == "ActorLocationStale",
            $"ST-F5 expected removed Message Follow route to fail stale, got '{stale.ErrorKind}'.");
        var evidence = await context.GetEvidenceAsync(finalTarget);
        ZlinkStreamAssert.Ensure(
            !evidence.Any(item => SpotActorTransferScenarioContext.EvidenceText(item)
                .Contains($"ST-F5|{actorId}|packet_handler|after-route-removal", StringComparison.Ordinal)),
            "ST-F5 packet reached the target handler after Message Follow route removal.");
    }
}
