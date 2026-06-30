using DiscoveryRegistryHa.Client.Support;
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

    private static async Task VerifyRegistryAsync(
        string name,
        string registryUrl,
        string consumerUrl,
        ClientOptions options)
    {
        using var registry = ZLinkHttpClient.Create(registryUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var consumer = ZLinkHttpClient.Create(consumerUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        await registry.Post("/registry/members/wait")
            .Body(new MemberEndpointWaitReq(options.ApiAEndpoint))
            .SubmitRawAsync();

        var marker = $"dr-b3-{name}-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileReq("dr-b3", marker))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-b3", $"DR-B3 {name} request failed.");
        ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", $"DR-B3 {name} routed to an unexpected provider.");
        ScenarioAssert.That(reply.Marker == marker, $"DR-B3 {name} marker mismatch.");

        var evidenceClient = reply.ProviderRid == "api-a" ? providerA : providerB;
        var evidence = (await evidenceClient.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                                 && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            $"DR-B3 {name} provider evidence was not recorded.");
    }
}