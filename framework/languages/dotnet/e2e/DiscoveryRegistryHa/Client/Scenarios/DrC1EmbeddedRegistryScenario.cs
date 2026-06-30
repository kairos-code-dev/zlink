using System.Text.Json;
using DiscoveryRegistryHa.Client.Support;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-C1 verifies discovery continues through a live registry after another
// registry is stopped, and the dead registry endpoint fails within a bound.
internal static class DrC1EmbeddedRegistryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var reg1 = ZLinkHttpClient.Create(options.Reg1Url)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var consumer = ZLinkHttpClient.Create(options.Reg1ConsumerUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

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

    private static async Task AssertDeadRegistryFailsAsync(string reg2Url)
    {
        using var deadRegistry = ZLinkHttpClient.Create(reg2Url)
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