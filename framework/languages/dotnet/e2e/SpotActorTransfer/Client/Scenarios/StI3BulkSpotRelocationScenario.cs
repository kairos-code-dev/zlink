using System.Diagnostics;
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StI3BulkSpotRelocationScenario
{
    internal static Task RunInstanceAsync(
        SpotActorTransferScenarioContext context) =>
        RunAsync(context, instanceSpot: true);

    internal static Task RunSpotWideAsync(
        SpotActorTransferScenarioContext context) =>
        RunAsync(context, instanceSpot: false);

    private static async Task RunAsync(
        SpotActorTransferScenarioContext context,
        bool instanceSpot)
    {
        var countVariable = instanceSpot
            ? "ZLINK_E2E_ST_I3_INSTANCE_COUNT"
            : "ZLINK_E2E_ST_I3_SPOTWIDE_COUNT";
        var canonicalCount = instanceSpot ? 1_000 : 100;
        var spotCount = RelocationWorkloadEnvironment.Count(
            countVariable,
            canonicalCount);
        var actorsPerSpot = instanceSpot
            ? 0
            : RelocationWorkloadEnvironment.Count(
                "ZLINK_E2E_ST_I3_ACTORS_PER_SPOT",
                100);
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
        var label = instanceSpot ? "instance" : "spotwide";
        var runId = Guid.NewGuid().ToString("N");

        var controlSpotId =
            $"st-i3-{label}-control-spot-{runId}";
        var controlActorPrefix =
            $"st-i3-{label}-control-actor-{runId}";
        await context.SetExclusivePlacementAsync(context.NodeC);
        try
        {
            _ = await context.CreatePayloadUserSpotAsync(
                context.NodeC,
                controlSpotId,
                new RelocationPayloadSpotReq(
                    $"ST-I3-{label}-control",
                    4 * 1024));
            _ = await context.CreateBulkActorsAsync(
                context.NodeC,
                new RelocationBulkActorCreateReq(
                    $"ST-I3-{label}-control",
                    controlActorPrefix,
                    SpotActorTransferNames.ActorTypeNoAdapter,
                    1,
                    0,
                    setupConcurrency));
        }
        finally
        {
            await context.RestoreDefaultPlacementAsync();
        }
        var controlActorId =
            controlActorPrefix + "-000000";

        RelocationBulkSpotCreateRes moving;
        await context.SetExclusivePlacementAsync(context.NodeA);
        try
        {
            moving = await context.CreateBulkSpotsAsync(
                context.NodeA,
                new RelocationBulkSpotCreateReq(
                    $"ST-I3-{label}",
                    $"st-i3-{label}-{runId}",
                    spotCount,
                    instanceSpot ? 64 * 1024 : 1024 * 1024,
                    instanceSpot,
                    MaxConcurrency: setupConcurrency,
                    ActorsPerSpot: actorsPerSpot));
        }
        finally
        {
            await context.RestoreDefaultPlacementAsync();
        }
        ZlinkStreamAssert.Ensure(
            moving.SpotIds.Length == spotCount
            && moving.NodeRids.All(rid =>
                SpotActorTransferScenarioContext.IsNode(
                    rid,
                    "actor-a"))
            && (instanceSpot
                ? moving.ActorIds.Length == 0
                : moving.ActorIds.Length
                  == spotCount * actorsPerSpot),
            $"ST-I3 {label} bulk was not created on source actor-a.");
        var initialLocations =
            await context.GetRelocationLocationsAsync(
                context.NodeB,
                moving.ActorIds,
                moving.SpotIds);

        var baselineActor = new RelocationBulkWorkload(
            context,
            $"ST-I3-{label}-control-actor-baseline",
            "actor",
            [controlActorId],
            rate);
        var baselineSpot = new RelocationBulkWorkload(
            context,
            $"ST-I3-{label}-control-spot-baseline",
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
        var traffic = new List<Task<RelocationBulkWorkloadResult>>
        {
            new RelocationBulkWorkload(
                    context,
                    $"ST-I3-{label}-control-actor-relocation",
                    "actor",
                    [controlActorId],
                    rate)
                .RunAsync(
                    TimeSpan.FromMinutes(6),
                    relocationTraffic.Token),
            new RelocationBulkWorkload(
                    context,
                    $"ST-I3-{label}-control-spot-relocation",
                    "spot",
                    [controlSpotId],
                    rate)
                .RunAsync(
                    TimeSpan.FromMinutes(6),
                    relocationTraffic.Token),
            new RelocationBulkWorkload(
                    context,
                    $"ST-I3-{label}-moving-spot-relocation",
                    "spot",
                    moving.SpotIds,
                    rate)
                .RunAsync(
                    TimeSpan.FromMinutes(6),
                    relocationTraffic.Token)
        };
        if (moving.ActorIds.Length > 0)
        {
            traffic.Add(
                new RelocationBulkWorkload(
                        context,
                        $"ST-I3-{label}-moving-actor-relocation",
                        "actor",
                        moving.ActorIds,
                        rate)
                    .RunAsync(
                        TimeSpan.FromMinutes(6),
                        relocationTraffic.Token));
        }

        var relocationWatch = Stopwatch.StartNew();
        var relocation = await context.RelocateAsync(
            context.NodeA,
            TimeSpan.FromMinutes(5));
        relocationWatch.Stop();
        relocationTraffic.Cancel();
        var during = await Task.WhenAll(traffic);

        ZlinkStreamAssert.Ensure(
            relocation.Outcome == "Relocated"
            && relocation.State == "Relocated",
            $"ST-I3 {label} host relocation did not reach "
            + $"Relocated: {relocation.Outcome}/"
            + $"{relocation.Reason}/{relocation.State}");
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
        var unitsPerSecond = spotCount / elapsed;
        var canonical =
            spotCount == canonicalCount
            && (instanceSpot || actorsPerSpot == 100)
            && baselineDuration == TimeSpan.FromSeconds(60)
            && rate == 200;
        Console.WriteLine(
            $"ST-I3 profile={label}"
            + $" unit_count={spotCount}"
            + $" participant_count="
            + (spotCount + moving.ActorIds.Length)
            + $" completed={spotCount}"
            + " safe_aborted=0 blocked=0"
            + $" elapsed_seconds={elapsed:F3}"
            + $" units_per_second={unitsPerSecond:F2}"
            + $" canonical_profile={canonical}");
        if (canonical)
        {
            var elapsedLimit = instanceSpot ? 150 : 180;
            var rateLimit = instanceSpot ? 8 : 1;
            ZlinkStreamAssert.Ensure(
                elapsed <= elapsedLimit
                && unitsPerSecond >= rateLimit,
                $"ST-I3 {label} workload_slo_missed.");
        }
        await RelocationBulkWorkloadVerification
            .VerifyRelocationTerminalsAsync(
                context,
                initialLocations,
                moving.ActorIds,
                moving.SpotIds,
                during,
                requireSpotWideAggregatePublication:
                    !instanceSpot);
    }
}
