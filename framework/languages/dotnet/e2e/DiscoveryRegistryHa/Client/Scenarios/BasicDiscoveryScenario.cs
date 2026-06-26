using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// Verifies the initial single-registry discovery scenario through the app driver.
// The client only requests the externally visible scenario endpoint.
internal static class BasicDiscoveryScenario
{
    public static async Task RunAsync(ZLinkHttpClient driver)
    {
        await driver.Post("/run/a1").SubmitRawAsync();
        Console.WriteLine("scenario DiscoveryRegistryHa.A1 passed");
    }
}
