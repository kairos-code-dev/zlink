using DiscoveryRegistryHa.Client.Support;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-D3 verifies an embedded registry/provider peered with a standalone
// registry can expose merged members and route a consumer request.
internal static class DrD3DirectEndpointScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var embedded = ZLinkHttpClient.Create(options.EmbeddedUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var consumer = ZLinkHttpClient.Create(options.EmbeddedConsumerUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        await embedded.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitReq(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-d3-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileReq("dr-d3", marker))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-d3", "DR-D3 request failed.");
        ScenarioAssert.That(
            reply.ProviderRid is "api-a" or "api-b" or "embedded-api-mixed",
            "DR-D3 routed to an unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, "DR-D3 marker mismatch.");

        var evidenceClient = reply.ProviderRid switch
        {
            "api-a" => providerA,
            "api-b" => providerB,
            _ => embedded
        };
        var evidence = (await evidenceClient.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                                 && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            "DR-D3 provider evidence was not recorded.");

        Console.WriteLine("scenario DR-D3 passed");
    }
}