// Verifies ST-F4 Message Follow relay and expiry rejection.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StF4MessageFollowThenRejectScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        const string scenario = "ST-F4";
        var actorId = $"actor-message-follow-{Guid.NewGuid():N}";
        var spotId = $"spot-message-follow-{Guid.NewGuid():N}";
        var created = await context.CreateActorAsync(
            context.NodeA,
            actorId,
            SpotActorTransferNames.ActorTypeStateful,
            104);
        var source = context.NodeForRid(created.NodeRid);
        var (target, _) = context.OtherActorNode(created.NodeRid);
        var targetSpot = await context.CreateSpotAsync(target, spotId);

        var g1 = Guid.NewGuid().ToString("N");
        var g2 = Guid.NewGuid().ToString("N");
        await context.ArmTransportDeliveryAsync(
            source, g1, actorId, "OneWay");
        await context.ArmTransportDeliveryAsync(
            source, g2, actorId, "Request");
        var g1Delivery = context.SendFromNodeAsync(
            source,
            actorId,
            new HandoffPacket(scenario, "G1"),
            g1);
        var g2Delivery = context.ProbeFromNodeAsync(
            source,
            actorId,
            new ProbeReq(scenario, "G2"),
            TimeSpan.FromSeconds(15),
            g2);
        await context.WaitTransportDeliveryAsync(source, g1);
        await context.WaitTransportDeliveryAsync(source, g2);

        ZlinkStreamAssert.Ensure(
            (await context.JoinAsync(
                source,
                actorId,
                new JoinTargetReq(scenario, spotId))).Accepted,
            "ST-F4 relocation was rejected.");
        await context.WaitEvidenceAsync(
            target,
            [$"{scenario}|{actorId}|success_reply|{spotId}"]);
        _ = await context.WaitActorOwnerAsync(
            target,
            actorId,
            targetSpot.NodeRid);

        await context.ReleaseTransportDeliveryAsync(source, g1);
        await g1Delivery;
        await context.WaitEvidenceAsync(
            target,
            [$"{scenario}|{actorId}|handoff_packet|G1"]);
        var deliveredEvidence = await context.GetEvidenceAsync(target);
        ZlinkStreamAssert.Ensure(
            deliveredEvidence.Count(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "handoff_packet"
                && item.Value == "G1") == 1,
            "ST-F4 duration-bounded delivery was not handled exactly once.");
        ZlinkStreamAssert.Ensure(
            !(await context.GetEvidenceAsync(source)).Any(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "handoff_packet"
                && item.Value == "G1"),
            "ST-F4 source application handler processed followed work.");

        // This topology configures a seven-second Message Follow duration.
        // The application-level assertion below proves expiry without reading
        // runtime markers or private route state.
        await Task.Delay(TimeSpan.FromSeconds(8));
        await context.ReleaseTransportDeliveryAsync(source, g2);
        var stale = await g2Delivery;
        ZlinkStreamAssert.Ensure(!stale.Succeeded && stale.ErrorKind == "InvalidOperation",
            $"ST-F4 expected InvalidOperation for a stale actor location, got '{stale.ErrorKind}'.");
        ZlinkStreamAssert.Ensure(
            !(await context.GetEvidenceAsync(target)).Any(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "packet_handler"
                && item.Value == "G2"),
            "ST-F4 expired request reached the target application handler.");
        ZlinkStreamAssert.Ensure(
            !(await context.GetEvidenceAsync(source)).Any(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "packet_handler"
                && item.Value == "G2"),
            "ST-F4 expired request reached the source application handler.");
        ZlinkStreamAssert.Ensure(
            (await context.GetTransportDeliveryAsync(source, g1)).ReleasedCount == 1
            && (await context.GetTransportDeliveryAsync(source, g2)).ReleasedCount == 1,
            "ST-F4 did not release each pre-resolved delivery exactly once.");
    }
}
