using ObservabilityOps.Client.Scenarios;
using ObservabilityOps.Client.Support;

var options = ClientOptions.Parse(args);
using var context = new ScenarioContext(options);
var scenarios = new Dictionary<string, Func<Task>>(StringComparer.OrdinalIgnoreCase)
{
    ["OBS-A1"] = () => ObsA1FlowCorrelationScenario.RunAsync(context),
    ["OBS-A2"] = () => ObsA2ErrorFlowScenario.RunAsync(context),
    ["OBS-A3"] = () => ObsA3FlowPropagationScenario.RunAsync(context),
    ["OBS-A4"] = () => ObsA4FanoutAndTimerScenario.RunAsync(context),
    ["OBS-B1"] = () => ObsB1ConnectionMetricsScenario.RunAsync(context),
    ["OBS-B2"] = () => ObsB2QueueAndTransferMetricsScenario.RunAsync(context),
    ["OBS-B3"] = () => ObsB3FanoutAndLeaseMetricsScenario.RunAsync(context),
    ["OBS-B4"] = () => ObsB4DisabledMetricsScenario.RunAsync(context),
    ["OBS-C1"] = () => ObsC1DrainingMarkerScenario.RunAsync(context),
    ["OBS-C2"] = () => ObsC2ActorHandoffScenario.RunAsync(context),
    ["OBS-C3"] = () => ObsC3FixedDrainScenario.RunAsync(context),
    ["OBS-C4"] = () => ObsC4ForcedSessionDrainScenario.RunAsync(context),
    ["OBS-C5"] = () => ObsC5RolloutScenario.RunAsync(context)
};
IEnumerable<string> selected = string.Equals(options.Scenario, "all", StringComparison.OrdinalIgnoreCase)
    ? scenarios.Keys : options.Scenario.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
foreach (var name in selected)
{
    if (!scenarios.TryGetValue(name, out var scenario)) throw new ArgumentException($"Unknown scenario '{name}'.");
    await scenario();
}
Console.WriteLine("observability-ops client result=passed");
