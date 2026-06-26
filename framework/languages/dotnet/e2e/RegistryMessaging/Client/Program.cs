using RegistryMessaging.Client;
using RegistryMessaging.Client.Scenarios;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);

using var driver = ZLinkHttpClient.Create(options.DriverUrl)
    .Json()
    .Timeout(TimeSpan.FromMinutes(5))
    .Build();

await RegistryMessagingScenario.RunAsync(driver);

Console.WriteLine("registry-messaging e2e result=passed");
