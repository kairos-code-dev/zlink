using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;
using DiscoveryRegistryHa.Client.Support;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-B2 verifies a consumer configured with a stopped registry and a live
// registry still discovers a provider through the live endpoint.
internal static class DrB2FailoverScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var reg1 = ZLinkHttpClient.Create(options.Reg1Url)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var consumer = ZLinkHttpClient.Create(options.Reg1Reg2ConsumerUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        await reg1.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitRequest(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-b2-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("dr-b2", marker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-b2", "DR-B2 request failed.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", "DR-B2 routed to an unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, "DR-B2 marker mismatch.");

        var evidenceClient = reply.ProviderRid == "api-a" ? providerA : providerB;
        var evidence = (await evidenceClient.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            "DR-B2 provider evidence was not recorded.");

        Console.WriteLine("scenario DR-B2 passed");
    }

}
