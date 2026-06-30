using DiscoveryRegistryHa.Client.Support;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// Verifies the single-registry baseline: the consumer discovers one of two
// providers through reg-1, sends a request, and the provider records evidence.
internal static class BasicDiscoveryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var registry = ZLinkHttpClient.Create(options.Reg1Url)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var consumer = ZLinkHttpClient.Create(options.ConsumerUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        await registry.Post("/registry/topology/wait")
            .Body(new TopologyReadyWaitReq(2))
            .SubmitRawAsync();

        var marker = $"dr-a1-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileReq("dr-a1", marker))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-a1", "DR-A1 request failed.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", "DR-A1 unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, "DR-A1 marker mismatch.");

        var provider = reply.ProviderRid == "api-a" ? providerA : providerB;
        var evidence = await WaitForEvidenceAsync(provider, marker);
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                                 && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            "DR-A1 provider evidence was not recorded.");

        Console.WriteLine("scenario DiscoveryRegistryHa.A1 passed");
    }

    private static async Task<string[]> WaitForEvidenceAsync(ZLinkHttpClient client, string contains)
    {
        return (await client.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(contains))
            .SubmitAsync<string[]>()).Body;
    }
}