using RuntimeMonitoring.Client;
using RuntimeMonitoring.Client.Scenarios;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);

using var driver = ZLinkHttpClient.Create(options.DriverUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(10))
    .Build();

await RuntimeMonitoringScenario.RunAsync(driver);

Console.WriteLine("runtime-monitoring client result=passed");
