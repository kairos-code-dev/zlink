using DiscoveryRegistryHa.Client.Support;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-C2 verifies a restarted registry rejoins the peer cluster and can route a
// consumer request through its recovered discovery view.
internal static class DrC2EmbeddedRegistryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var reg2 = ZLinkHttpClient.Create(options.Reg2Url)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var consumer = ZLinkHttpClient.Create(options.Reg2ConsumerUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        await reg2.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitReq(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-c2-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileReq("dr-c2", marker))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-c2", "DR-C2 request failed.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", "DR-C2 routed to an unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, "DR-C2 marker mismatch.");

        var evidenceClient = reply.ProviderRid == "api-a" ? providerA : providerB;
        var evidence = (await evidenceClient.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                                 && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            "DR-C2 provider evidence was not recorded.");

        Console.WriteLine("scenario DR-C2 passed");
    }
}