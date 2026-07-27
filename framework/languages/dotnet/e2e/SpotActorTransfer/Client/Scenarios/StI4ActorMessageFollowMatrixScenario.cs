using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StI4ActorMessageFollowMatrixScenario
{
    public static async Task RunAsync(
        SpotActorTransferScenarioContext context)
    {
        var scenario = "ST-I4";
        var actorId =
            $"actor-message-follow-matrix-{Guid.NewGuid():N}";
        var spotId =
            $"spot-message-follow-matrix-{Guid.NewGuid():N}";
        var created = await context.CreateActorAsync(
            context.NodeA,
            actorId,
            SpotActorTransferNames.ActorTypeStateful,
            stateVersion: 404,
            applicationStateBytes: 4 * 1024);
        var source = context.NodeForRid(created.NodeRid);
        var (target, targetPrefix) =
            context.OtherActorNode(created.NodeRid);
        await context.CreateSpotAsync(target, spotId);

        var queueOperation = Guid.NewGuid().ToString("N");
        await context.ArmTransportDeliveryAsync(
            source,
            queueOperation,
            actorId,
            "OneWay");
        var queued = context.SendFromNodeAsync(
            source,
            actorId,
            new HandoffPacket(scenario, "actor-one-way-source-baseline"),
            queueOperation);
        await context.WaitTransportDeliveryAsync(source, queueOperation);
        await context.ReleaseTransportDeliveryAsync(source, queueOperation);
        await queued;
        await context.WaitEvidenceAsync(
            source,
            [$"{scenario}|{actorId}|handoff_packet|actor-one-way-source-baseline"]);

        var oneWayOperation = Guid.NewGuid().ToString("N");
        var requestOperation = Guid.NewGuid().ToString("N");
        await context.ArmTransportDeliveryAsync(
            source, oneWayOperation, actorId, "OneWay");
        await context.ArmTransportDeliveryAsync(
            source, requestOperation, actorId, "Request");
        var oneWay = context.SendFromNodeAsync(
            source,
            actorId,
            new HandoffPacket(scenario, "actor-one-way-follow"),
            oneWayOperation);
        var request = context.ProbeFromNodeAsync(
            source,
            actorId,
            new ProbeReq(scenario, "actor-request-follow"),
            TimeSpan.FromSeconds(10),
            requestOperation);
        await context.WaitTransportDeliveryAsync(source, oneWayOperation);
        await context.WaitTransportDeliveryAsync(source, requestOperation);

        var joined = await context.JoinAsync(
            source,
            actorId,
            new JoinTargetReq(scenario, spotId));
        ZlinkStreamAssert.Ensure(
            joined.Accepted,
            $"{scenario} relocation was rejected.");

        await context.ReleaseTransportDeliveryAsync(
            source, oneWayOperation);
        await context.ReleaseTransportDeliveryAsync(
            source, requestOperation);
        await oneWay;
        var result = await request;
        ZlinkStreamAssert.Ensure(
            result.Succeeded && result.Reply is not null,
            $"{scenario} followed request failed: {result.ErrorKind}.");
        var reply = result.Reply!;
        ZlinkStreamAssert.Ensure(
            SpotActorTransferScenarioContext.IsNode(
                reply.NodeRid,
                targetPrefix),
            $"{scenario} request reached unexpected owner '{reply.NodeRid}'.");
        ZlinkStreamAssert.Ensure(
            reply.StateVersion == 404,
            $"{scenario} request lost restored Actor state.");

        await context.WaitRuntimeEvidenceAsync(source,
            $"message_follow_relay actor={actorId}");
        var targetEvidence = await context.WaitEvidenceAsync(
            target,
            [
                $"{scenario}|{actorId}|handoff_packet|actor-one-way-follow",
                $"{scenario}|{actorId}|packet_handler|actor-request-follow"
            ]);
        ZlinkStreamAssert.Ensure(
            targetEvidence.Count(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "handoff_packet"
                && item.Value == "actor-one-way-follow") == 1,
            $"{scenario} one-way was not handled exactly once.");
        ZlinkStreamAssert.Ensure(
            targetEvidence.Count(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "packet_handler"
                && item.Value == "actor-request-follow") == 1,
            $"{scenario} request was not handled exactly once.");
        ZlinkStreamAssert.Ensure(
            !(await context.GetEvidenceAsync(source)).Any(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && (item.Kind == "handoff_packet"
                    || item.Kind == "packet_handler")
                && item.Value.Contains(
                    "follow",
                    StringComparison.Ordinal)),
            $"{scenario} source application handler processed followed work.");
        ZlinkStreamAssert.Ensure(
            (await context.GetTransportDeliveryAsync(
                source, oneWayOperation)).ReleasedCount == 1
            && (await context.GetTransportDeliveryAsync(
                source, requestOperation)).ReleasedCount == 1,
            $"{scenario} delivery fixture did not preserve both operations.");
    }
}
