using ResilienceLifecycle.Client.Scenarios;
using ResilienceLifecycle.Client.Support;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);

using var consumer = ZLinkHttpClient.Create(options.ConsumerUrl)
    .Timeout(TimeSpan.FromMinutes(10))
    .Build();
using var registry = ZLinkHttpClient.Create(options.TopologyUrl)
    .Timeout(TimeSpan.FromMinutes(10))
    .Build();
await using var processes = new ResilienceProcessManager(options);

using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl)
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();

var scenarios = new (string Name, Func<Task> Run)[]
{
    ("RL-A1", () => RlA1ProviderRestartScenario.RunAsync(consumer, registry, processes, providerA, providerB)),
    ("RL-A2", () => RlA2ProviderEndpointRemapScenario.RunAsync(
        consumer, registry, processes, providerA, providerB)),
    ("RL-A3", () => RlA3ReconnectStormScenario.RunAsync(
        consumer, registry, processes, providerA, providerB)),
    ("RL-A4", () => RlA4DrainAndGreenEndpointScenario.RunAsync(
        consumer, registry, processes, providerA, providerB)),
    ("RL-A5", () => RlA5ProviderFlappingScenario.RunAsync(consumer, registry, processes, providerA, providerB)),
    ("RL-B1", () => RlB1CancellationCleanupScenario.RunAsync(consumer, providerA, providerB)),
    ("RL-B2", () => RlB2CrashDuringInflightScenario.RunAsync(consumer, registry, processes, providerA, providerB)),
    ("RL-B3", () => RlB3GracefulShutdownScenario.RunAsync(
        consumer, registry, processes, providerA, providerB)),
    ("RL-B4", () => RlB4RuntimeDrainScenario.RunAsync(consumer, providerA, providerB)),
    ("RL-B5", () => RlB5DrainInflightScenario.RunAsync(consumer, providerA, providerB)),
    ("RL-B6", () => RlB6GrayFaultScenario.RunAsync(consumer, providerA, providerB)),
    ("RL-C1", () => RlC1ClientHostLifecycleScenario.RunAsync(consumer, providerA, providerB)),
    ("RL-C2", () => RlC2TopologyRecoveryScenario.RunAsync(consumer, registry, processes, providerA, providerB)),
    ("RL-C3", () => RlC3NodePauseRecoveryScenario.RunAsync(consumer, registry, processes, providerA, providerB)),
    ("RL-C4", () => RlC4RegistryOutageScenario.RunAsync(consumer, registry, processes, providerA, providerB)),
    ("RL-D1", () => RlD1HighFanoutScenario.RunAsync(consumer, providerA, providerB)),
    ("RL-D2", () => RlD2ObserverFaultScenario.RunAsync(consumer, providerA, providerB)),
    ("RL-D3", () => RlD3DispatchErrorEvidenceScenario.RunAsync(consumer, providerA, providerB)),
    ("RL-D4", () => RlD4MissingRequestHandlerScenario.RunAsync(consumer, providerA, providerB)),
    ("RL-D5", () => RlD5MixedBurstScenario.RunAsync(consumer, providerA, providerB))
};

if (string.Equals(options.Scenario, "all", StringComparison.OrdinalIgnoreCase))
{
    foreach (var scenario in scenarios) await scenario.Run();
}
else
{
    foreach (var scenarioName in options.Scenario.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
    {
        var selected = scenarios.FirstOrDefault(scenario =>
            string.Equals(scenario.Name, scenarioName, StringComparison.OrdinalIgnoreCase));
        if (selected.Run is null) throw new ArgumentException($"Unknown scenario '{scenarioName}'.");

        await selected.Run();
    }
}

Console.WriteLine("resilience-lifecycle client result=passed");
