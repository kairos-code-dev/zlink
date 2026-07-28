using SpotActorTransfer.Client.Scenarios;
using SpotActorTransfer.Client.Support;

var options = ClientOptions.Parse(args);
using var context = new SpotActorTransferScenarioContext(options);
await context.WaitMeshReadyAsync();

var scenarios = new Dictionary<string, Func<Task>>(StringComparer.OrdinalIgnoreCase)
{
    ["ST-A1"] = () => StA1LocalAcceptScenario.RunAsync(context),
    ["ST-A2"] = () => StA2LocalRejectScenario.RunAsync(context),
    ["ST-A3"] = () => StA3MovingDispatchBlockedScenario.RunAsync(context),
    ["ST-B1"] = () => StB1RemoteStatefulTransferScenario.RunAsync(context),
    ["ST-B2"] = () => StB2SourceCleanupFailureAfterSuccessScenario.RunAsync(context),
    ["ST-B3"] = () => StB3MissingAdapterScenario.RunAsync(context),
    ["ST-B4"] = () => StB4EmptyStateTransferScenario.RunAsync(context),
    ["ST-C1"] = () => StC1SourceDownBeforeCommitScenario.RunAsync(context),
    ["ST-C2"] = () => StC2SourceDownAfterTargetCommitScenario.RunAsync(context),
    ["ST-C3"] = () => StC3CallbackFailureClassificationScenario.RunAsync(context),
    ["ST-D1"] = () => StD1LocationCommitTimingScenario.RunAsync(context),
    ["ST-D2"] = () => StD2StaleSourceReleaseFencingScenario.RunAsync(context),
    ["ST-E1"] = () => StE1BoundSessionPushAfterTransferScenario.RunAsync(context),
    ["ST-E1A"] = () => StE1ANewIncarnationExplicitBindScenario.RunAsync(context),
    ["ST-E2"] = () => StE2BoundSessionRebindIsolationScenario.RunAsync(context),
    ["ST-F1"] = () => StF1InFlightHandoffOrderScenario.RunAsync(context),
    ["ST-F2"] = () => StF2DirectOvertakePreventionScenario.RunAsync(context),
    ["ST-F3"] = () => StF3BoundSessionCrossMoveOrderScenario.RunAsync(context),
    ["ST-F4"] = () => StF4MessageFollowThenRejectScenario.RunAsync(context),
    ["ST-F5"] = () => StF5MessageFollowRouteRemovalScenario.RunAsync(context),
    ["ST-F6"] = () => StF6InFlightRequestCorrelationAndTimeoutScenario.RunAsync(context),
    ["ST-G3"] = () => StG3PerActorShellRelocationScenario.RunAsync(context),
    ["ST-G5-SMALL"] = () =>
        StG5ActorRelocationInterruptionScenario.RunSmallAsync(context),
    ["ST-G5-SLOW-CAPTURE"] = () =>
        StG5ActorRelocationInterruptionScenario.RunSlowCaptureAsync(context),
    ["ST-G5-SLOW-RESTORE"] = () =>
        StG5ActorRelocationInterruptionScenario.RunSlowRestoreAsync(context),
    ["ST-G5-SLOW-CLEANUP"] = () =>
        StG5ActorRelocationInterruptionScenario.RunSlowCleanupAsync(context),
    ["ST-G5-SPOT-WIDE-ACTORS-10"] = () =>
        StG5SpotWideRelocationInterruptionScenario.RunActors10Async(context),
    ["ST-G5-SPOT-WIDE-ACTORS-100"] = () =>
        StG5SpotWideRelocationInterruptionScenario.RunActors100Async(context),
    ["ST-I1"] = () => StI1RelocationPayloadMeasurementScenario.RunAsync(context),
    ["ST-I1-ACTOR-BOUNDARY"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunActorBoundaryRelocationOnlyAsync(context),
    ["ST-I1-INSTANCE"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunInstanceRelocationOnlyAsync(context),
    ["ST-I1-SPOTWIDE"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunSpotWideRelocationOnlyAsync(context),
    ["ST-I1-SPOTWIDE-SMALL"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunSpotWideSmallRelocationOnlyAsync(context),
    ["ST-I1-SPOTWIDE-BOUNDARY"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunSpotWideBoundaryRelocationOnlyAsync(context),
    ["ST-I2-RECREATE"] = () =>
        StI2BulkActorRelocationScenario.RunRecreateAsync(context),
    ["ST-I2-SNAPSHOT"] = () =>
        StI2BulkActorRelocationScenario.RunSnapshotAsync(context),
    ["ST-I3-INSTANCE"] = () =>
        StI3BulkSpotRelocationScenario.RunInstanceAsync(context),
    ["ST-I3-SPOTWIDE"] = () =>
        StI3BulkSpotRelocationScenario.RunSpotWideAsync(context),
    ["ST-I4"] = () => StI4ActorMessageFollowMatrixScenario.RunAsync(context),
    ["ST-I5"] = () => StI5MessageFollowSafetyScenario.RunAsync(context),
    ["ST-I6"] = () => StI6ActorMultiHopMessageFollowScenario.RunAsync(context)
};

var excludedFromAll = new HashSet<string>(
    [
        "ST-F4",
        "ST-F5",
        "ST-G3",
        "ST-G5-SMALL",
        "ST-G5-SLOW-CAPTURE",
        "ST-G5-SLOW-RESTORE",
        "ST-G5-SLOW-CLEANUP",
        "ST-G5-SPOT-WIDE-ACTORS-10",
        "ST-G5-SPOT-WIDE-ACTORS-100",
        "ST-I1",
        "ST-I1-ACTOR-BOUNDARY",
        "ST-I1-INSTANCE",
        "ST-I1-SPOTWIDE",
        "ST-I1-SPOTWIDE-SMALL",
        "ST-I1-SPOTWIDE-BOUNDARY",
        "ST-I2-RECREATE",
        "ST-I2-SNAPSHOT",
        "ST-I3-INSTANCE",
        "ST-I3-SPOTWIDE",
        "ST-I4",
        "ST-I5",
        "ST-I6"
    ],
    StringComparer.OrdinalIgnoreCase);
var selected = (string.Equals(options.Scenario, "all", StringComparison.OrdinalIgnoreCase)
    ? scenarios.Keys.Where(name => !excludedFromAll.Contains(name))
    : options.Scenario.Split(
        ',',
        StringSplitOptions.RemoveEmptyEntries
        | StringSplitOptions.TrimEntries))
    .ToArray();
if (selected.Any(static name =>
        name.Equals("ST-I2-RECREATE", StringComparison.OrdinalIgnoreCase)
        || name.Equals(
            "ST-I2-SNAPSHOT",
            StringComparison.OrdinalIgnoreCase))
    && selected.Length != 1)
{
    throw new ArgumentException(
        "Each ST-I2 profile requires its own fresh host-process run.");
}
if (selected.Any(static name => name.StartsWith(
        "ST-G5-SPOT-WIDE-",
        StringComparison.OrdinalIgnoreCase))
    && selected.Length != 1)
{
    throw new ArgumentException(
        "Each ST-G5 SpotWide profile requires its own fresh host-process run.");
}
var diagnosticOnlyRun = false;
foreach (var name in selected)
{
    if (!scenarios.TryGetValue(name, out var scenario))
        throw new ArgumentException($"Unknown scenario '{name}'.");
    await scenario();
    var diagnosticOnly = IsDiagnosticOnly(name);
    diagnosticOnlyRun |= diagnosticOnly;
    Console.WriteLine(
        diagnosticOnly
            ? $"operation SpotActorTransfer.{name} diagnostic_only"
            : $"operation SpotActorTransfer.{name} passed");
}

Console.WriteLine(
    diagnosticOnlyRun
        ? "spot-actor-transfer e2e result=diagnostic_only"
        : "spot-actor-transfer e2e result=passed");

static bool IsDiagnosticOnly(string name)
{
    return name.ToUpperInvariant() switch
    {
        // ST-I1 currently covers payload profiles and Store read-back only.
        // The selector must not report full completion until queue, journal,
        // timer, permit-contention, and aggregate-bound cases are implemented.
        "ST-I1" => true,
        // These selectors remain diagnostic until interruption and resource
        // measurements are recorded without private runtime hooks.
        "ST-I2-RECREATE" or "ST-I2-SNAPSHOT"
            or "ST-I3-INSTANCE" => true,
        // Final owner equality is not a SpotWide pre/post visibility proof.
        "ST-I3-SPOTWIDE" => true,
        // ACTORS-10 is the required public continuity gate. ACTORS-100
        // additionally owns the canonical one-second scale gate and remains
        // diagnostic until its separate publication and stale-route cases
        // are observable through public contracts.
        "ST-G5-SPOT-WIDE-ACTORS-100" => true,
        // These selectors cover only the currently connected Actor cases.
        // Spot, duplicate, bound, recovery, and route-cleanup cases remain.
        "ST-I4" or "ST-I5" or "ST-I6" => true,
        _ => false
    };
}
