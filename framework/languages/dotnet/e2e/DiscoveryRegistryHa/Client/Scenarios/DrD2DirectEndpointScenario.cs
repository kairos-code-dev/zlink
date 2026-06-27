using DiscoveryRegistryHa.Client;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-D2 verifies the standalone registry deployment can advertise providers
// and route a consumer request with the same public behavior as other layouts.
internal static class DrD2DirectEndpointScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var registry = CreateClient(options.Reg1Url);
        using var consumer = CreateClient(options.Reg1ConsumerUrl);
        using var providerA = CreateClient(options.ProviderAUrl);
        using var providerB = CreateClient(options.ProviderBUrl);

        await registry.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitRequest(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-d2-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("dr-d2", marker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-d2", "DR-D2 request failed.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", "DR-D2 routed to an unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, "DR-D2 marker mismatch.");

        var evidenceClient = reply.ProviderRid == "api-a" ? providerA : providerB;
        var evidence = (await evidenceClient.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            "DR-D2 provider evidence was not recorded.");

        Console.WriteLine("scenario DR-D2 passed");
    }

    static ZLinkHttpClient CreateClient(string baseUrl) =>
        ZLinkHttpClient.Create(baseUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

}
