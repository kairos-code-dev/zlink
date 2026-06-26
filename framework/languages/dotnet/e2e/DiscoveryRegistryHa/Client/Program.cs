using DiscoveryRegistryHa.Client;
using DiscoveryRegistryHa.Client.Scenarios;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);

using var driver = ZLinkHttpClient.Create(options.DriverUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(10))
    .Build();

if (options.Scenario is "a1")
{
    await BasicDiscoveryScenario.RunAsync(driver);
}
else if (options.Scenario is "cluster")
{
    await ClusterDiscoveryScenario.RunAsync(driver);
}
else
{
    throw new InvalidOperationException($"Unsupported scenario '{options.Scenario}'.");
}

Console.WriteLine($"discovery-registry-ha client scenario={options.Scenario} result=passed");
