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
    ["ST-I1"] = () => StI1RelocationPayloadMeasurementScenario.RunAsync(context),
    ["ST-I1-ACTOR-BOUNDARY"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunActorBoundaryRelocationOnlyAsync(context),
    ["ST-I1-INSTANCE"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunInstanceActivationOnlyAsync(context),
    ["ST-I1-SPOTWIDE"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunSpotWideRelocationOnlyAsync(context),
    ["ST-I1-SPOTWIDE-SMALL"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunSpotWideSmallRelocationOnlyAsync(context),
    ["ST-I1-SPOTWIDE-BOUNDARY"] = () =>
        StI1RelocationPayloadMeasurementScenario
            .RunSpotWideBoundaryRelocationOnlyAsync(context),
    ["ST-I2"] = () => StI2BulkActorRelocationScenario.RunAsync(context),
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
        "ST-I1",
        "ST-I1-ACTOR-BOUNDARY",
        "ST-I1-INSTANCE",
        "ST-I1-SPOTWIDE",
        "ST-I1-SPOTWIDE-SMALL",
        "ST-I1-SPOTWIDE-BOUNDARY",
        "ST-I2",
        "ST-I3-INSTANCE",
        "ST-I3-SPOTWIDE",
        "ST-I4",
        "ST-I5",
        "ST-I6"
    ],
    StringComparer.OrdinalIgnoreCase);
IEnumerable<string> selected = string.Equals(options.Scenario, "all", StringComparison.OrdinalIgnoreCase)
    ? scenarios.Keys.Where(name => !excludedFromAll.Contains(name))
    : options.Scenario.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
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
    static int Value(string variable, int fallback) =>
        int.TryParse(
            Environment.GetEnvironmentVariable(variable),
            out var value)
            ? value
            : fallback;

    var baseline = Value(
        "ZLINK_E2E_RELOCATION_BASELINE_SECONDS",
        60);
    var rate = Value("ZLINK_E2E_RELOCATION_RATE", 200);
    return name.ToUpperInvariant() switch
    {
        "ST-I2" =>
            Value("ZLINK_E2E_ST_I2_RECREATE_COUNT", 10_000) != 10_000
            || Value("ZLINK_E2E_ST_I2_SNAPSHOT_COUNT", 1_000) != 1_000
            || baseline != 60
            || rate != 200,
        "ST-I3-INSTANCE" =>
            Value("ZLINK_E2E_ST_I3_INSTANCE_COUNT", 1_000) != 1_000
            || baseline != 60
            || rate != 200,
        "ST-I3-SPOTWIDE" =>
            Value("ZLINK_E2E_ST_I3_SPOTWIDE_COUNT", 100) != 100
            || Value("ZLINK_E2E_ST_I3_ACTORS_PER_SPOT", 100) != 100
            || baseline != 60
            || rate != 200,
        _ => false
    };
}
