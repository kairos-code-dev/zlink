using System.Diagnostics;
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StI2BulkActorRelocationScenario
{
    public static async Task RunAsync(
        SpotActorTransferScenarioContext context)
    {
        const int canonicalRecreateCount = 10_000;
        const int canonicalSnapshotCount = 1_000;
        var recreateCount =
            RelocationWorkloadEnvironment.Count(
                "ZLINK_E2E_ST_I2_RECREATE_COUNT",
                canonicalRecreateCount);
        var snapshotCount =
            RelocationWorkloadEnvironment.Count(
                "ZLINK_E2E_ST_I2_SNAPSHOT_COUNT",
                canonicalSnapshotCount);
        var baselineDuration =
            RelocationWorkloadEnvironment.Duration(
                "ZLINK_E2E_RELOCATION_BASELINE_SECONDS",
                60);
        var rate = RelocationWorkloadEnvironment.Rate(
            "ZLINK_E2E_RELOCATION_RATE",
            200);
        var setupConcurrency =
            RelocationWorkloadEnvironment.Count(
                "ZLINK_E2E_RELOCATION_SETUP_CONCURRENCY",
                64);
        var runId = Guid.NewGuid().ToString("N");

        var controlSpotId = $"st-i2-control-spot-{runId}";
        var controlActorId = $"st-i2-control-actor-{runId}";
        await context.SetExclusivePlacementAsync(context.NodeC);
        try
        {
            _ = await context.CreatePayloadUserSpotAsync(
                context.NodeC,
                controlSpotId,
                new RelocationPayloadSpotReq("ST-I2-control", 4 * 1024));
            _ = await context.CreateBulkActorsAsync(
                context.NodeC,
                new RelocationBulkActorCreateReq(
                    "ST-I2-control",
                    controlActorId,
                    SpotActorTransferNames.ActorTypeNoAdapter,
                    1,
                    0,
                    setupConcurrency));
        }
        finally
        {
            await context.RestoreDefaultPlacementAsync();
        }
        controlActorId += "-000000";

        RelocationBulkActorCreateRes recreate;
        RelocationBulkActorCreateRes snapshot;
        await context.SetExclusivePlacementAsync(context.NodeA);
        try
        {
            recreate = await context.CreateBulkActorsAsync(
                context.NodeA,
                new RelocationBulkActorCreateReq(
                    "ST-I2-recreate",
                    $"st-i2-recreate-{runId}",
                    SpotActorTransferNames.ActorTypeNoAdapter,
                    recreateCount,
                    0,
                    setupConcurrency));
            snapshot = await context.CreateBulkActorsAsync(
                context.NodeA,
                new RelocationBulkActorCreateReq(
                    "ST-I2-snapshot",
                    $"st-i2-snapshot-{runId}",
                    SpotActorTransferNames.ActorTypeStateful,
                    snapshotCount,
                    64 * 1024,
                    setupConcurrency));
        }
        finally
        {
            await context.RestoreDefaultPlacementAsync();
        }

        EnsureOwnedBySource(recreate, "Recreate");
        EnsureOwnedBySource(snapshot, "Snapshot");
        var movingActorIds =
            recreate.ActorIds.Concat(snapshot.ActorIds).ToArray();
        var initialLocations =
            await context.GetRelocationLocationsAsync(
                context.NodeB,
                movingActorIds,
                []);

        var baselineActor = new RelocationBulkWorkload(
            context,
            "ST-I2-control-actor-baseline",
            "actor",
            [controlActorId],
            rate);
        var baselineSpot = new RelocationBulkWorkload(
            context,
            "ST-I2-control-spot-baseline",
            "spot",
            [controlSpotId],
            rate);
        var baseline = await Task.WhenAll(
            baselineActor.RunAsync(baselineDuration),
            baselineSpot.RunAsync(baselineDuration));
        foreach (var result in baseline)
        {
            await RelocationBulkWorkloadVerification.VerifyAsync(
                context,
                result);
            RelocationBulkWorkloadVerification.Report(result);
        }

        using var relocationTraffic = new CancellationTokenSource();
        var relocationActor = new RelocationBulkWorkload(
            context,
            "ST-I2-control-actor-relocation",
            "actor",
            [controlActorId],
            rate);
        var relocationSpot = new RelocationBulkWorkload(
            context,
            "ST-I2-control-spot-relocation",
            "spot",
            [controlSpotId],
            rate);
        var movingActors = new RelocationBulkWorkload(
            context,
            "ST-I2-moving-actor-relocation",
            "actor",
            movingActorIds,
            rate);
        var trafficTasks = new[]
        {
            relocationActor.RunAsync(
                TimeSpan.FromMinutes(6),
                relocationTraffic.Token),
            relocationSpot.RunAsync(
                TimeSpan.FromMinutes(6),
                relocationTraffic.Token),
            movingActors.RunAsync(
                TimeSpan.FromMinutes(6),
                relocationTraffic.Token)
        };

        var relocationWatch = Stopwatch.StartNew();
        var relocation = await context.RelocateAsync(
            context.NodeA,
            TimeSpan.FromMinutes(5));
        relocationWatch.Stop();
        relocationTraffic.Cancel();
        var during = await Task.WhenAll(trafficTasks);

        ZlinkStreamAssert.Ensure(
            relocation.Outcome == "Relocated"
            && relocation.State == "Relocated",
            "ST-I2 host relocation did not reach Relocated: "
            + $"{relocation.Outcome}/{relocation.Reason}/"
            + relocation.State);
        foreach (var result in during)
        {
            await RelocationBulkWorkloadVerification.VerifyAsync(
                context,
                result);
            RelocationBulkWorkloadVerification.Report(result);
        }
        RelocationBulkWorkloadVerification.VerifyContinuity(
            baseline[0],
            during[0]);
        RelocationBulkWorkloadVerification.VerifyContinuity(
            baseline[1],
            during[1]);

        var elapsed = relocationWatch.Elapsed.TotalSeconds;
        var recreateRate = recreateCount / elapsed;
        var snapshotRate = snapshotCount / elapsed;
        var canonical =
            recreateCount == canonicalRecreateCount
            && snapshotCount == canonicalSnapshotCount
            && baselineDuration == TimeSpan.FromSeconds(60)
            && rate == 200;
        Console.WriteLine(
            $"ST-I2 unit_count={recreateCount + snapshotCount}"
            + $" completed={movingActorIds.Length}"
            + " safe_aborted=0 blocked=0"
            + $" elapsed_seconds={elapsed:F3}"
            + $" recreate_units_per_second={recreateRate:F2}"
            + $" snapshot_units_per_second={snapshotRate:F2}"
            + $" canonical_profile={canonical}");
        if (canonical)
        {
            ZlinkStreamAssert.Ensure(
                elapsed <= 180
                && recreateRate >= 64
                && snapshotRate >= 16,
                "ST-I2 workload_slo_missed.");
        }
        await RelocationBulkWorkloadVerification
            .VerifyRelocationTerminalsAsync(
                context,
                initialLocations,
                movingActorIds,
                [],
                during,
                requireSpotWideAggregatePublication: false);
    }

    private static void EnsureOwnedBySource(
        RelocationBulkActorCreateRes result,
        string kind)
    {
        ZlinkStreamAssert.Ensure(
            result.ActorIds.Length == result.NodeRids.Length
            && result.NodeRids.All(rid =>
                SpotActorTransferScenarioContext.IsNode(
                    rid,
                    "actor-a")),
            $"ST-I2 {kind} bulk was not placed on source actor-a.");
    }
}
