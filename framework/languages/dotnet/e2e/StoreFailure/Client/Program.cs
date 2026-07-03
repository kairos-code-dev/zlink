using StoreFailure.Client.Scenarios;
using StoreFailure.Client.Support;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);

using var consumer = ZLinkHttpClient.Create(options.ConsumerUrl)
    .Timeout(TimeSpan.FromMinutes(10))
    .Build();
using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl).Build();
using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl).Build();
await using var processes = new StoreFailureProcessManager(options);

// api-b lives under the client so scenarios can SIGKILL and restart it;
// the run script only owns the store, api-a, and the consumer.
var providerBProcess = await processes.StartProviderBAsync();

async Task RestartProviderBAsync()
{
    providerBProcess = await processes.StartProviderBAsync();
    await SfProbe.WaitPeersAsync(
        consumer,
        peers => SfProbe.HasRid(peers, "api-b"),
        options.OwnerLeaseTtl + options.HeartbeatInterval * 4,
        "setup: restarted api-b did not re-register.");
}

var scenarios = new (string Name, Func<Task> Run)[]
{
    ("SF-A1", () => SfA1BaselineScenario.RunAsync(options, consumer, providerA, providerB)),
    ("SF-A2", () => SfA2PollingFallbackScenario.RunAsync(options, processes)),
    ("SF-B1", () => SfB1FailStaticScenario.RunAsync(options, consumer, processes)),
    ("SF-B2", () => SfB2GraceExceededScenario.RunAsync(options, consumer, processes)),
    ("SF-D1", () => SfD1ShortOutageRecoveryScenario.RunAsync(options, consumer, processes)),
    ("SF-D3", () => SfD3StatusTransitionScenario.RunAsync(options, consumer, processes)),
    ("SF-C2", async () =>
    {
        await SfC2GracefulRemovalScenario.RunAsync(options, consumer, providerBProcess);
        await RestartProviderBAsync();
    }),
    ("SF-C1", async () =>
    {
        await SfC1CrashLeaseExpiryScenario.RunAsync(options, consumer, processes, providerBProcess);
        await RestartProviderBAsync();
    }),
    ("SF-D2", () => SfD2LongOutageRecoveryScenario.RunAsync(options, consumer, processes, providerBProcess))
};

foreach (var (name, run) in scenarios)
{
    if (options.Scenario != "all"
        && !string.Equals(options.Scenario, name, StringComparison.OrdinalIgnoreCase))
        continue;

    await run();
}

Console.WriteLine("store-failure client result=passed");
