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
        var firstSpotRef = await context.CreateSpotAsync(
            firstTarget,
            firstSpot);
        var finalSpotRef = await context.CreateSpotAsync(
            finalTarget,
            finalSpot);

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
        await context.WaitEvidenceAsync(
            firstTarget,
            [$"{scenario}|{actorId}|success_reply|{firstSpot}"]);
        _ = await context.WaitActorOwnerAsync(
            firstTarget,
            actorId,
            firstSpotRef.NodeRid);
        ZlinkStreamAssert.Ensure((await context.JoinAsync(
                firstTarget,
                actorId,
                new JoinTargetReq(scenario, finalSpot))).Accepted,
            "ST-F5 chained transfer was rejected.");
        await context.WaitEvidenceAsync(
            finalTarget,
            [$"{scenario}|{actorId}|success_reply|{finalSpot}"]);

        await context.ReleaseTransportDeliveryAsync(source, chainOperation);
        await chained;
        await context.WaitEvidenceAsync(
            finalTarget,
            [$"{scenario}|{actorId}|handoff_packet|chain-to-final"]);
        _ = await context.WaitActorOwnerAsync(
            finalTarget,
            actorId,
            finalSpotRef.NodeRid);
        var finalEvidence = await context.GetEvidenceAsync(finalTarget);
        ZlinkStreamAssert.Ensure(
            finalEvidence.Count(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "handoff_packet"
                && item.Value == "chain-to-final") == 1,
            "ST-F5 multi-hop delivery was not handled exactly once.");
        foreach (var previousOwner in new[] { source, firstTarget })
        {
            ZlinkStreamAssert.Ensure(
                !(await context.GetEvidenceAsync(previousOwner)).Any(item =>
                    item.Scenario == scenario
                    && item.ActorId == actorId
                    && item.Kind == "handoff_packet"
                    && item.Value == "chain-to-final"),
                "ST-F5 previous-owner application handler processed followed work.");
        }

        // Expiry is verified through the public terminal result. Route table
        // cleanup itself has no public observation surface.
        await Task.Delay(TimeSpan.FromSeconds(8));
        await context.ReleaseTransportDeliveryAsync(
            source, expiredOperation);
        var stale = await expired;
        ZlinkStreamAssert.Ensure(!stale.Succeeded && stale.ErrorKind == "InvalidOperation",
            $"ST-F5 expected removed Message Follow route to fail stale, got '{stale.ErrorKind}'.");
        foreach (var node in new[] { source, firstTarget, finalTarget })
        {
            ZlinkStreamAssert.Ensure(
                !(await context.GetEvidenceAsync(node)).Any(item =>
                    item.Scenario == scenario
                    && item.ActorId == actorId
                    && item.Kind == "packet_handler"
                    && item.Value == "after-route-removal"),
                "ST-F5 expired request reached an application handler.");
        }
        ZlinkStreamAssert.Ensure(
            (await context.GetTransportDeliveryAsync(
                source, chainOperation)).ReleasedCount == 1
            && (await context.GetTransportDeliveryAsync(
                source, expiredOperation)).ReleasedCount == 1,
            "ST-F5 did not release each pre-resolved operation exactly once.");
    }
}
