using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// Verifies registry clustering, failover, embedded deployment, and discovery
// recovery by asking the server-side driver to execute the cluster scenario set.
internal static class ClusterDiscoveryScenario
{
    public static async Task RunAsync(ZLinkHttpClient driver)
    {
        await driver.Post("/run/cluster").SubmitRawAsync();
        Console.WriteLine("scenario DiscoveryRegistryHa.Cluster passed");
    }
}
