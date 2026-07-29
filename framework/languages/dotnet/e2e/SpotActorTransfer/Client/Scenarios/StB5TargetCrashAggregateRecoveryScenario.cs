// Verifies target-process replacement for one published SpotWide aggregate.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StB5TargetCrashAggregateRecoveryScenario
{
    internal static async Task RunAsync(
        SpotActorTransferScenarioContext context)
    {
        const string scenario = "ST-B5";
        var suffix = Guid.NewGuid().ToString("N");
        var spotIdPrefix = $"st-b5-target-crash-{suffix}";

        RelocationBulkSpotCreateRes created;
        await context.SetExclusivePlacementAsync(context.NodeA);
        try
        {
            created = await context.CreateBulkSpotsAsync(
                context.NodeA,
                new RelocationBulkSpotCreateReq(
                    scenario,
                    spotIdPrefix,
                    Count: 1,
                    ApplicationStateBytes: 4 * 1024,
                    InstanceSpot: false,
                    MaxConcurrency: 1,
                    ActorsPerSpot: 1,
                    PerActor: false,
                    ActorApplicationStateBytes: 4 * 1024));
        }
        finally
        {
            await context.RestoreDefaultPlacementAsync();
        }

        var spotId = created.SpotIds.Single();
        var actorId = created.ActorIds.Single();
        var initial = await context.GetRelocationLocationsAsync(
            context.NodeC,
            [actorId],
            [spotId]);
        ZlinkStreamAssert.Ensure(
            initial.Count == 2
            && initial.All(location =>
                SpotActorTransferScenarioContext.IsNode(
                    location.NodeRid,
                    "actor-a")),
            $"{scenario} did not create the complete source aggregate.");

        await context.SetExclusivePlacementAsync(context.NodeB);
        var targetProbe = await context.CreateBulkSpotsAsync(
            context.NodeA,
            new RelocationBulkSpotCreateReq(
                scenario + "-TARGET-PROBE",
                $"st-b5-target-probe-{suffix}",
                Count: 1,
                ApplicationStateBytes: 0,
                InstanceSpot: false,
                MaxConcurrency: 1));
        var failedTargetRid = targetProbe.NodeRids.Single();
        await context.ClosePayloadSpotAsync(
            context.NodeA,
            targetProbe.SpotIds.Single());
        try
        {
            _ = await context.RelocateAsync(
                context.NodeA,
                TimeSpan.FromSeconds(20));
        }
        catch
        {
            // The runner terminates the selected target after authority
            // publication. The original source request may lose its reply.
        }

        var recovered = await WaitForRecoveredAggregateAsync(
            context,
            actorId,
            spotId,
            failedTargetRid);
        foreach (var location in recovered)
        {
            var original = initial.Single(item =>
                item.ObjectId == location.ObjectId);
            ZlinkStreamAssert.Ensure(
                location.ObjectGeneration == original.ObjectGeneration,
                $"{scenario} changed ObjectGeneration for "
                + $"'{location.ObjectId}'.");
            ZlinkStreamAssert.Ensure(
                SpotActorTransferScenarioContext.IsNode(
                    location.NodeRid,
                    "actor-b"),
                $"{scenario} did not recover '{location.ObjectId}' on "
                + "the restarted target process.");
        }

        // The source is Draining after RelocateAsync, so it is not required
        // to establish a new peer connection to the replacement process.
        // Probe through the serving observer and require the recovered global
        // location to dispatch both Spot and Actor traffic.
        var spotRouteFailures = await WaitForServiceRouteAsync(
            context,
            spotId,
            actor: false,
            failedTargetRid);
        var actorRouteFailures = await WaitForServiceRouteAsync(
            context,
            actorId,
            actor: true,
            failedTargetRid);

        var spotTraffic = await new RelocationBulkWorkload(
                context,
                scenario + "-SPOT",
                "spot",
                [spotId],
                10,
                context.NodeC)
            .PrimeAllTargetsAsync(1);
        var actorTraffic = await new RelocationBulkWorkload(
                context,
                scenario + "-ACTOR",
                "actor",
                [actorId],
                10,
                context.NodeC)
            .PrimeAllTargetsAsync(1);
        await RelocationBulkWorkloadVerification.VerifyAsync(
            context,
            spotTraffic);
        await RelocationBulkWorkloadVerification.VerifyAsync(
            context,
            actorTraffic);

        var evidence = await context.GetEvidenceAsync(context.NodeB);
        ZlinkStreamAssert.Ensure(
            evidence.Count(item =>
                item.ActorId == spotId
                && item.Kind == "spot_application_state_restored") == 1,
            $"{scenario} restored the User Spot callback more than once.");
        ZlinkStreamAssert.Ensure(
            evidence.Count(item =>
                item.ActorId == actorId
                && item.Kind == "application_state_restored") == 1,
            $"{scenario} restored the member Actor callback more than once.");
        ZlinkStreamAssert.Ensure(
            evidence.Count(item =>
                item.Scenario is "runtime"
                && item.Kind.Contains(
                    "callback",
                    StringComparison.OrdinalIgnoreCase)) == 0,
            $"{scenario} exposed an infrastructure callback.");

        Console.WriteLine(
            $"required_gate=spotwide_target_crash_recovery status=passed"
            + $" spot={spotId} actor={actorId}"
            + " object_generation=preserved"
            + " restore_duplicate=0 service_available=true"
            + " routing=global_location_after_recovery"
            + $" route_probe_failures={spotRouteFailures + actorRouteFailures}");
    }

    private static async Task<int> WaitForServiceRouteAsync(
        SpotActorTransferScenarioContext context,
        string targetId,
        bool actor,
        string failedTargetRid)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(15);
        var failedAttempts = 0;
        Exception? lastFailure = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var locations = await context.GetRelocationLocationsAsync(
                context.NodeC,
                actor ? [targetId] : [],
                actor ? [] : [targetId]);
            var location = locations.Single();
            ZlinkStreamAssert.Ensure(
                !StringComparer.Ordinal.Equals(
                    location.NodeRid,
                    failedTargetRid)
                && SpotActorTransferScenarioContext.IsNode(
                    location.NodeRid,
                    "actor-b"),
                $"ST-B5 route probe observed stale authority for '{targetId}'.");

            var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            var request = new RelocationWorkloadCallReq(
                targetId,
                actor ? "ST-B5-ACTOR-ROUTE" : "ST-B5-SPOT-ROUTE",
                failedAttempts + 1,
                Guid.NewGuid().ToString("N"),
                now,
                now + 1_000,
                TimeoutMilliseconds: 1_000);
            try
            {
                var reply = actor
                    ? await context.RequestActorWorkloadAsync(
                        context.NodeC,
                        request)
                    : await context.RequestSpotWorkloadAsync(
                        context.NodeC,
                        request);
                if (reply.TargetId == targetId
                    && reply.OperationId == request.OperationId
                    && reply.ObjectGeneration == location.ObjectGeneration)
                    return failedAttempts;
                lastFailure = new InvalidOperationException(
                    "Route probe reply did not match the recovered authority.");
            }
            catch (Exception error)
            {
                lastFailure = error;
            }

            failedAttempts++;
            Console.WriteLine(
                $"route_probe_retry target={targetId}"
                + $" attempt={failedAttempts}"
                + $" error={lastFailure.Message}");
            await Task.Delay(100);
        }

        throw new TimeoutException(
            $"ST-B5 service route did not converge for '{targetId}' "
            + $"after {failedAttempts} failed attempts.",
            lastFailure);
    }

    private static async Task<IReadOnlyList<RelocationLocationSnapshot>>
        WaitForRecoveredAggregateAsync(
            SpotActorTransferScenarioContext context,
            string actorId,
            string spotId,
            string failedTargetRid)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                var locations =
                    await context.GetRelocationLocationsAsync(
                        context.NodeC,
                        [actorId],
                        [spotId]);
                if (locations.Count == 2
                    && locations.All(location =>
                        SpotActorTransferScenarioContext.IsNode(
                            location.NodeRid,
                            "actor-b")
                        && !StringComparer.Ordinal.Equals(
                            location.NodeRid,
                            failedTargetRid)))
                    return locations;
            }
            catch (Exception error)
            {
                last = error;
            }
            await Task.Delay(100);
        }
        throw new TimeoutException(
            "ST-B5 aggregate did not recover on the restarted target.",
            last);
    }
}
