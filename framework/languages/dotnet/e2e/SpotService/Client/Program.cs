using SpotService.Client;
using SpotService.Client.Scenarios;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);

using var driver = ZLinkHttpClient.Create(options.DriverUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();

await SpotServiceScenario.RunAsync(driver, options.ScenarioSet);

Console.WriteLine($"spot-service client scenario_set={options.ScenarioSet} result=passed");
