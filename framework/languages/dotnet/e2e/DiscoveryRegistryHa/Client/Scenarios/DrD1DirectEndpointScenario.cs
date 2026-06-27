using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;
using DiscoveryRegistryHa.Client.Support;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-D1 verifies an embedded registry+provider process exposes the same
// discovery and messaging behavior through its role endpoints.
internal static class DrD1DirectEndpointScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var embedded = ZLinkHttpClient.Create(options.EmbeddedUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var consumer = ZLinkHttpClient.Create(options.EmbeddedConsumerUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        var marker = $"dr-d1-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("dr-d1", marker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-d1", "DR-D1 request failed.");
        ScenarioAssert.That(reply.ProviderRid == "embedded-api", "DR-D1 should route to embedded-api.");
        ScenarioAssert.That(reply.Marker == marker, "DR-D1 marker mismatch.");

        var evidence = (await embedded.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                && line.Contains("rid=embedded-api", StringComparison.Ordinal)),
            "DR-D1 embedded provider evidence was not recorded.");

        Console.WriteLine("scenario DR-D1 passed");
    }

}
