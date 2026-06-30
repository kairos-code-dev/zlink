using DiscoveryRegistryHa.Client.Support;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-A2 verifies a reg-2-only consumer reaches a provider advertised through reg-1.
internal static class DrA2ClusterBridgeScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var reg2 = ZLinkHttpClient.Create(options.Reg2Url)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var consumer = ZLinkHttpClient.Create(options.ConsumerUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        await reg2.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitRequest(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-a2-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("dr-a2", marker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-a2", "DR-A2 request failed.");
        ScenarioAssert.That(reply.ProviderRid == "api-a", "DR-A2 request should route to api-a.");
        ScenarioAssert.That(reply.Marker == marker, "DR-A2 marker mismatch.");

        var evidence = (await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                                 && line.Contains("rid=api-a", StringComparison.Ordinal)),
            "DR-A2 api-a evidence was not recorded.");

        Console.WriteLine("scenario DR-A2 passed");
    }
}