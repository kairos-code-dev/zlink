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
    ["ST-F4"] = () => StF4StragglerForwardThenFailFastScenario.RunAsync(context),
    ["ST-F5"] = () => StF5ForwardingMappingEvictionScenario.RunAsync(context),
    ["ST-F6"] = () => StF6InFlightRequestCorrelationAndTimeoutScenario.RunAsync(context)
};

IEnumerable<string> selected = string.Equals(options.Scenario, "all", StringComparison.OrdinalIgnoreCase)
    ? scenarios.Keys
    : options.Scenario.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
foreach (var name in selected)
{
    if (!scenarios.TryGetValue(name, out var scenario))
        throw new ArgumentException($"Unknown scenario '{name}'.");
    await scenario();
    Console.WriteLine($"operation SpotActorTransfer.{name} passed");
}

Console.WriteLine("spot-actor-transfer e2e result=passed");
