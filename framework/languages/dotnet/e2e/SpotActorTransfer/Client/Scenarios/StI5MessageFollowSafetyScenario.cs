using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StI5MessageFollowSafetyScenario
{
    public static async Task RunAsync(
        SpotActorTransferScenarioContext context)
    {
        var scenario = "ST-I5";
        var actorId =
            $"actor-message-follow-safety-{Guid.NewGuid():N}";
        var spotId =
            $"spot-message-follow-safety-{Guid.NewGuid():N}";
        var created = await context.CreateActorAsync(
            context.NodeA,
            actorId,
            SpotActorTransferNames.ActorTypeStateful,
            stateVersion: 505,
            applicationStateBytes: 4 * 1024);
        var source = context.NodeForRid(created.NodeRid);
        var (target, targetPrefix) =
            context.OtherActorNode(created.NodeRid);
        var targetSpot = await context.CreateSpotAsync(target, spotId);

        var correlationA = Guid.NewGuid().ToString("N");
        var correlationB = Guid.NewGuid().ToString("N");
        var expiredOperation = Guid.NewGuid().ToString("N");
        var deadlineOperation = Guid.NewGuid().ToString("N");
        await context.ArmTransportDeliveryAsync(
            source, correlationA, actorId, "Request");
        await context.ArmTransportDeliveryAsync(
            source, correlationB, actorId, "Request");
        await context.ArmTransportDeliveryAsync(
            source, expiredOperation, actorId, "Request");
        await context.ArmTransportDeliveryAsync(
            source, deadlineOperation, actorId, "Request");

        var first = context.ProbeFromNodeAsync(
            source,
            actorId,
            new ProbeReq(scenario, "correlation-a"),
            TimeSpan.FromSeconds(15),
            correlationA);
        var second = context.ProbeFromNodeAsync(
            source,
            actorId,
            new ProbeReq(scenario, "correlation-b"),
            TimeSpan.FromSeconds(15),
            correlationB);
        var expired = context.ProbeFromNodeAsync(
            source,
            actorId,
            new ProbeReq(scenario, "expired"),
            TimeSpan.FromSeconds(15),
            expiredOperation);
        var deadline = context.ProbeFromNodeAsync(
            source,
            actorId,
            new ProbeReq(scenario, "deadline"),
            TimeSpan.FromSeconds(5),
            deadlineOperation);
        await context.WaitTransportDeliveryAsync(source, correlationA);
        await context.WaitTransportDeliveryAsync(source, correlationB);
        await context.WaitTransportDeliveryAsync(source, expiredOperation);
        await context.WaitTransportDeliveryAsync(source, deadlineOperation);

        ZlinkStreamAssert.Ensure(
            (await context.JoinAsync(
                source,
                actorId,
                new JoinTargetReq(scenario, spotId))).Accepted,
            $"{scenario} relocation was rejected.");
        await context.WaitEvidenceAsync(
            target,
            [$"{scenario}|{actorId}|success_reply|{spotId}"]);
        _ = await context.WaitActorOwnerAsync(
            target,
            actorId,
            targetSpot.NodeRid);

        // Release the two stale requests in reverse order and verify each
        // terminal before admitting the deadline probe. This keeps the
        // correlation assertion independent from the Actor's serial handler
        // queue while still proving that Message Follow does not exchange
        // reply capabilities.
        await context.ReleaseTransportDeliveryAsync(source, correlationB);
        var secondReply = await second;
        await context.ReleaseTransportDeliveryAsync(source, correlationA);
        var firstReply = await first;
        await context.ReleaseTransportDeliveryAsync(
            source, deadlineOperation);
        var replies = new[] { firstReply, secondReply };
        ZlinkStreamAssert.Ensure(
            replies[0].Succeeded
            && replies[1].Succeeded
            && replies[0].Reply?.Marker == "correlation-a"
            && replies[1].Reply?.Marker == "correlation-b"
            && replies.All(result =>
                result.Reply is not null
                &&
                SpotActorTransferScenarioContext.IsNode(
                    result.Reply.NodeRid,
                    targetPrefix)),
            $"{scenario} request correlation crossed between followed calls: "
            + string.Join(
                ";",
                replies.Select(result =>
                    $"success={result.Succeeded},error={result.ErrorKind},"
                    + $"marker={result.Reply?.Marker},node={result.Reply?.NodeRid}")));
        var correlationEvidence = await context.WaitEvidenceAsync(
            target,
            [
                $"{scenario}|{actorId}|packet_handler|correlation-a",
                $"{scenario}|{actorId}|packet_handler|correlation-b"
            ]);
        foreach (var marker in new[] { "correlation-a", "correlation-b" })
        {
            ZlinkStreamAssert.Ensure(
                correlationEvidence.Count(item =>
                    item.Scenario == scenario
                    && item.ActorId == actorId
                    && item.Kind == "packet_handler"
                    && item.Value == marker) == 1,
                $"{scenario} request '{marker}' was not handled exactly once.");
        }

        var deadlineResult = await deadline;
        ZlinkStreamAssert.Ensure(
            !deadlineResult.Succeeded
            && deadlineResult.ErrorKind == nameof(TimeoutException),
            $"{scenario} followed request extended its original deadline: "
            + deadlineResult.ErrorKind);
        var deadlineEvidence = await context.WaitEvidenceAsync(
            target,
            [$"{scenario}|{actorId}|packet_handler|deadline"]);
        ZlinkStreamAssert.Ensure(
            deadlineEvidence.Count(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "packet_handler"
                && item.Value == "deadline") == 1,
            $"{scenario} deadline request did not enter Message Follow exactly once.");
        // The test topology uses a seven-second Message Follow duration.
        // Releasing a delivery after this bounded wait proves the public stale
        // terminal without consulting private route cleanup markers.
        await Task.Delay(TimeSpan.FromSeconds(8));
        ZlinkStreamAssert.Ensure(
            (await context.GetEvidenceAsync(target)).Count(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "late_reply_created"
                && item.Value == "deadline") == 1,
            $"{scenario} did not exercise exactly one reply created after the original deadline.");
        ZlinkStreamAssert.Ensure(
            !deadlineResult.Succeeded
            && deadlineResult.ErrorKind == nameof(TimeoutException),
            $"{scenario} changed the completed timeout after the late reply.");
        await context.ReleaseTransportDeliveryAsync(
            source, expiredOperation);
        var expiredResult = await expired;
        ZlinkStreamAssert.Ensure(
            !expiredResult.Succeeded
            && expiredResult.ErrorKind == "ActorLocationStale",
            $"{scenario} expired delivery was not rejected as stale: "
            + expiredResult.ErrorKind);
        ZlinkStreamAssert.Ensure(
            !(await context.GetEvidenceAsync(target)).Any(item =>
                item.Scenario == scenario
                && item.ActorId == actorId
                && item.Kind == "packet_handler"
                && item.Value == "expired"),
            $"{scenario} expired request reached the target application handler.");
        var sourceEvidence = await context.GetEvidenceAsync(source);
        foreach (var marker in new[]
                 {
                     "correlation-a",
                     "correlation-b",
                     "deadline",
                     "expired"
                 })
        {
            ZlinkStreamAssert.Ensure(
                !sourceEvidence.Any(item =>
                    item.Scenario == scenario
                    && item.ActorId == actorId
                    && item.Kind == "packet_handler"
                    && item.Value == marker),
                $"{scenario} source handler processed followed request '{marker}'.");
        }
        foreach (var operationId in new[]
                 {
                     correlationA,
                     correlationB,
                     expiredOperation,
                     deadlineOperation
                 })
        {
            ZlinkStreamAssert.Ensure(
                (await context.GetTransportDeliveryAsync(
                    source,
                    operationId)).ReleasedCount == 1,
                $"{scenario} operation '{operationId}' was not released exactly once.");
        }

        // MF-DUP, MF-GEN, MF-LOOP, MF-HOP and MF-BOUND remain partial. They
        // require controlled duplicate delivery or additional committed
        // generations; this gate deliberately does not mutate or synthesize
        // private routes.
    }
}
