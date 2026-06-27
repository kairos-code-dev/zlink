using DiscoveryRegistryHa.Client;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-C2 verifies a restarted registry rejoins the peer cluster and can route a
// consumer request through its recovered discovery view.
internal static class DrC2EmbeddedRegistryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var reg2 = CreateClient(options.Reg2Url);
        using var consumer = CreateClient(options.Reg2ConsumerUrl);
        using var providerA = CreateClient(options.ProviderAUrl);
        using var providerB = CreateClient(options.ProviderBUrl);

        await reg2.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitRequest(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-c2-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("dr-c2", marker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-c2", "DR-C2 request failed.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", "DR-C2 routed to an unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, "DR-C2 marker mismatch.");

        var evidenceClient = reply.ProviderRid == "api-a" ? providerA : providerB;
        var evidence = (await evidenceClient.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            "DR-C2 provider evidence was not recorded.");

        Console.WriteLine("scenario DR-C2 passed");
    }

    static ZLinkHttpClient CreateClient(string baseUrl) =>
        ZLinkHttpClient.Create(baseUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

}
