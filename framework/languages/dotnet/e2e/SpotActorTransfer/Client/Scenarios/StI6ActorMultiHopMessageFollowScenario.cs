// Verifies Actor Message Follow across multiple relocation hops without duplicate handling.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StI6ActorMultiHopMessageFollowScenario
{
    public static async Task RunAsync(
        SpotActorTransferScenarioContext context)
    {
        var scenario = "ST-I6";
        var actorId =
            $"actor-message-follow-multi-hop-{Guid.NewGuid():N}";
        var firstSpotId =
            $"spot-message-follow-hop-one-{Guid.NewGuid():N}";
        var secondSpotId =
            $"spot-message-follow-hop-two-{Guid.NewGuid():N}";
        var created = await context.CreateActorAsync(
            context.NodeA,
            actorId,
            SpotActorTransferNames.ActorTypeStateful,
            stateVersion: 606,
            applicationStateBytes: 4 * 1024);
        var source = context.NodeForRid(created.NodeRid);
        var (firstTarget, _) =
            context.OtherActorNode(created.NodeRid);
        var secondTarget =
            context.ThirdActorNode(source, firstTarget);
        var secondTargetPrefix =
            ReferenceEquals(secondTarget, context.NodeA)
                ? "actor-a"
                : ReferenceEquals(secondTarget, context.NodeB)
                    ? "actor-b"
                    : "actor-c";

        var firstSpot = await context.CreateSpotAsync(
            firstTarget,
            firstSpotId);
        var secondSpot = await context.CreateSpotAsync(
            secondTarget,
            secondSpotId);
        var operationId = Guid.NewGuid().ToString("N");
        await context.ArmTransportDeliveryAsync(
            source,
            operationId,
            actorId,
            "Request");
        var delayedRequest = context.ProbeFromNodeAsync(
            source,
            actorId,
            new ProbeReq(scenario, "multi-hop-request"),
            TimeSpan.FromSeconds(15),
            operationId);
        await context.WaitTransportDeliveryAsync(source, operationId);

        ZlinkStreamAssert.Ensure(
            (await context.JoinAsync(
                source,
                actorId,
                new JoinTargetReq(scenario, firstSpotId))).Accepted,
            $"{scenario} first relocation was rejected.");
        await context.WaitEvidenceAsync(
            firstTarget,
            [$"{scenario}|{actorId}|success_reply|{firstSpotId}"]);
        _ = await context.WaitActorOwnerAsync(
            firstTarget,
            actorId,
            firstSpot.NodeRid);
        ZlinkStreamAssert.Ensure(
            (await context.JoinAsync(
                firstTarget,
                actorId,
                new JoinTargetReq(scenario, secondSpotId))).Accepted,
            $"{scenario} second relocation was rejected.");
        await context.WaitEvidenceAsync(
            secondTarget,
            [$"{scenario}|{actorId}|success_reply|{secondSpotId}"]);
        _ = await context.WaitActorOwnerAsync(
            secondTarget,
            actorId,
            secondSpot.NodeRid);

        await context.ReleaseTransportDeliveryAsync(source, operationId);
        var delayedResult = await delayedRequest;
        ZlinkStreamAssert.Ensure(
            delayedResult.Succeeded && delayedResult.Reply is not null,
            $"{scenario} delayed request failed: {delayedResult.ErrorKind}.");
        var reply = delayedResult.Reply!;
        ZlinkStreamAssert.Ensure(
            SpotActorTransferScenarioContext.IsNode(
                reply.NodeRid,
                secondTargetPrefix),
            $"{scenario} multi-hop request reached '{reply.NodeRid}'.");
        ZlinkStreamAssert.Ensure(
            reply.StateVersion == 606,
            $"{scenario} multi-hop relocation lost Actor state.");

        var targetEvidence = await context.WaitEvidenceAsync(
            secondTarget,
            [$"{scenario}|{actorId}|packet_handler|multi-hop-request"]);
        ZlinkStreamAssert.Ensure(
            targetEvidence.Count(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "packet_handler"
                && item.Value == "multi-hop-request") == 1,
            $"{scenario} multi-hop request was not handled exactly once.");
        foreach (var previousOwner in new[] { source, firstTarget })
        {
            ZlinkStreamAssert.Ensure(
                !(await context.GetEvidenceAsync(previousOwner)).Any(item =>
                    item.Scenario == scenario
                    && item.ActorId == actorId
                    && item.Kind == "packet_handler"
                    && item.Value == "multi-hop-request"),
                $"{scenario} previous-owner application handler processed followed work.");
        }
        ZlinkStreamAssert.Ensure(
            (await context.GetTransportDeliveryAsync(
                source,
                operationId)).ReleasedCount == 1,
            $"{scenario} pre-resolved operation was not released exactly once.");
    }
}
