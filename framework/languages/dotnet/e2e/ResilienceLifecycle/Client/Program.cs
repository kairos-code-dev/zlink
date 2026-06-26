using ResilienceLifecycle.Client;
using ResilienceLifecycle.Client.Scenarios;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);

using var driver = ZLinkHttpClient.Create(options.DriverUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(10))
    .Build();

await ResilienceLifecycleScenario.RunAsync(driver);

Console.WriteLine("resilience-lifecycle client result=passed");
