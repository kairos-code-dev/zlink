using DiscoveryRegistryHa.Client;
using DiscoveryRegistryHa.Shared;
using System.Text.Json;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-C1 verifies discovery continues through a live registry after another
// registry is stopped, and the dead registry endpoint fails within a bound.
internal static class DrC1EmbeddedRegistryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var reg1 = CreateClient(options.Reg1Url);
        using var consumer = CreateClient(options.Reg1ConsumerUrl);
        using var providerA = CreateClient(options.ProviderAUrl);
        using var providerB = CreateClient(options.ProviderBUrl);

        await reg1.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitRequest(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-c1-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("dr-c1", marker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-c1", "DR-C1 request failed.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", "DR-C1 routed to an unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, "DR-C1 marker mismatch.");

        var evidenceClient = reply.ProviderRid == "api-a" ? providerA : providerB;
        var evidence = (await evidenceClient.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            "DR-C1 provider evidence was not recorded.");

        await AssertDeadRegistryFailsAsync(options.Reg2Url);

        Console.WriteLine("scenario DR-C1 passed");
    }

    static ZLinkHttpClient CreateClient(string baseUrl) =>
        ZLinkHttpClient.Create(baseUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

    static async Task AssertDeadRegistryFailsAsync(string reg2Url)
    {
        using var deadRegistry = ZLinkHttpClient.Create(reg2Url)
            .Json()
            .Timeout(TimeSpan.FromMilliseconds(500))
            .Build();
        try
        {
            _ = await deadRegistry.Get("/registry/status").SubmitAsync<JsonElement>();
        }
        catch
        {
            return;
        }

        throw new InvalidOperationException("DR-C1 dead registry endpoint did not fail within the bounded timeout.");
    }
}
