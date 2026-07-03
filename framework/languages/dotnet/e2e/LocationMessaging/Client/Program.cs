using LocationMessaging.Client.Scenarios;
using LocationMessaging.Client.Support;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);

using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl)
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var workflow = ZLinkHttpClient.Create(options.WorkflowUrl)
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var directConsumer = ZLinkHttpClient.Create(options.DirectConsumerUrl)
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var singleConsumer = ZLinkHttpClient.Create(options.SingleConsumerUrl)
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var storeConsumer = ZLinkHttpClient.Create(options.StoreConsumerUrl)
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();
using var backpressureConsumer = ZLinkHttpClient.Create(options.BackpressureConsumerUrl)
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();

var scenarios = new (string Name, Func<Task> Run)[]
{
    ("RM-A1", () => RmA1LocationStoreAutoConnectScenario.RunAsync(providerA, providerB)),
    ("RM-A2", () => RmA2ManualEndpointScenario.RunAsync(providerA)),
    ("RM-A4", () => RmA4SameRidFailoverScenario.RunAsync(options)),
    ("RM-A6", () => RmA6MultipleChannelsScenario.RunAsync(providerA, providerB, workflow)),
    ("RM-B1", () => RmB1ScaleOutScenario.RunAsync(options)),
    ("RM-B2", () => RmB2ScaleInScenario.RunAsync(options)),
    ("RM-C1", () => RmC1RequestSendScenario.RunAsync(providerA, providerB)),
    ("RM-C2", () => RmC2TargetedRouteScenario.RunAsync(providerA, providerB)),
    ("RM-C3", () => RmC3MultiProviderDistributionScenario.RunAsync(directConsumer, providerA, providerB)),
    ("RM-C4", () => RmC4TimeoutIsolationScenario.RunAsync(storeConsumer, providerA, providerB)),
    ("RM-C5", () => RmC5MissingPacketScenario.RunAsync(storeConsumer, providerA, providerB)),
    ("RM-C7", () => RmC7WeightedProviderScenario.RunAsync(options)),
    ("RM-C8", () => RmC8PayloadRoundTripScenario.RunAsync(singleConsumer, providerA)),
    ("RM-C9", () => RmC9BackpressureScenario.RunAsync(backpressureConsumer, providerA))
};

if (string.Equals(options.Scenario, "all", StringComparison.OrdinalIgnoreCase))
{
    foreach (var scenario in scenarios) await scenario.Run();
}
else
{
    var selected = scenarios.FirstOrDefault(scenario =>
        string.Equals(scenario.Name, options.Scenario, StringComparison.OrdinalIgnoreCase));
    if (selected.Run is null) throw new ArgumentException($"Unknown scenario '{options.Scenario}'.");

    await selected.Run();
}

Console.WriteLine("location-messaging e2e result=passed");