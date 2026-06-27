using DiscoveryRegistryHa.Client;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-B3 verifies that after registry peer flapping, the restarted registry and
// a survivor registry both expose provider discovery and route requests.
internal static class DrB3RecoveryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await VerifyRegistryAsync(
            "reg-2",
            options.Reg2Url,
            options.Reg2ConsumerUrl,
            options);
        await VerifyRegistryAsync(
            "survivor",
            options.Reg1Url,
            options.Reg1ConsumerUrl,
            options);

        Console.WriteLine("scenario DR-B3 passed");
    }

    static async Task VerifyRegistryAsync(
        string name,
        string registryUrl,
        string consumerUrl,
        ClientOptions options)
    {
        using var registry = CreateClient(registryUrl);
        using var consumer = CreateClient(consumerUrl);
        using var providerA = CreateClient(options.ProviderAUrl);
        using var providerB = CreateClient(options.ProviderBUrl);

        await registry.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitRequest(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-b3-{name}-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("dr-b3", marker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-b3", $"DR-B3 {name} request failed.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", $"DR-B3 {name} routed to an unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, $"DR-B3 {name} marker mismatch.");

        var evidenceClient = reply.ProviderRid == "api-a" ? providerA : providerB;
        var evidence = (await evidenceClient.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            $"DR-B3 {name} provider evidence was not recorded.");
    }

    static ZLinkHttpClient CreateClient(string baseUrl) =>
        ZLinkHttpClient.Create(baseUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

}
