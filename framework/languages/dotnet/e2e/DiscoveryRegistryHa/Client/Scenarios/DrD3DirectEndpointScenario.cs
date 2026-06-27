using DiscoveryRegistryHa.Client;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-D3 verifies an embedded registry/provider peered with a standalone
// registry can expose merged members and route a consumer request.
internal static class DrD3DirectEndpointScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var embedded = CreateClient(options.EmbeddedUrl);
        using var consumer = CreateClient(options.EmbeddedConsumerUrl);
        using var providerA = CreateClient(options.ProviderAUrl);
        using var providerB = CreateClient(options.ProviderBUrl);

        await embedded.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitRequest(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-d3-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("dr-d3", marker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-d3", "DR-D3 request failed.");
        ScenarioAssert.That(
            reply.ProviderRid is "api-a" or "api-b" or "embedded-api-mixed",
            "DR-D3 routed to an unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, "DR-D3 marker mismatch.");

        var evidenceClient = reply.ProviderRid switch
        {
            "api-a" => providerA,
            "api-b" => providerB,
            _ => embedded,
        };
        var evidence = (await evidenceClient.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            "DR-D3 provider evidence was not recorded.");

        Console.WriteLine("scenario DR-D3 passed");
    }

    static ZLinkHttpClient CreateClient(string baseUrl) =>
        ZLinkHttpClient.Create(baseUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

}
