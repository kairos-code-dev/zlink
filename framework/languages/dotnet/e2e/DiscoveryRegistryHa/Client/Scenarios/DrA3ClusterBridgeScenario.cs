using DiscoveryRegistryHa.Client;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-A3 verifies each registry in a three-node peer cluster can see both
// providers through the merged member view and route a consumer request.
internal static class DrA3ClusterBridgeScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        var cases = new[]
        {
            new RegistryCase("reg-1", options.Reg1Url, options.Reg1ConsumerUrl),
            new RegistryCase("reg-2", options.Reg2Url, options.Reg2ConsumerUrl),
            new RegistryCase("reg-3", options.Reg3Url, options.Reg3ConsumerUrl),
        };

        using var providerA = CreateClient(options.ProviderAUrl);
        using var providerB = CreateClient(options.ProviderBUrl);
        foreach (var registryCase in cases)
        {
            using var registry = CreateClient(registryCase.RegistryUrl);
            using var consumer = CreateClient(registryCase.ConsumerUrl);

            await registry.Post("/registry/members/wait")
                .Body(new MemberEndpointWaitRequest(options.ApiAEndpoint))
                .SubmitRawAsync();
            await registry.Post("/registry/members/wait")
                .Body(new MemberEndpointWaitRequest(options.ApiBEndpoint))
                .SubmitRawAsync();

            var marker = $"dr-a3-{registryCase.Name}-{Guid.NewGuid():N}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("dr-a3", marker))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.Value == "profile:dr-a3", $"DR-A3 {registryCase.Name} request failed.");
            ScenarioAssert.That(
                reply.ProviderRid is "api-a" or "api-b",
                $"DR-A3 {registryCase.Name} routed to an unexpected provider.");
            ScenarioAssert.That(reply.Marker == marker, $"DR-A3 {registryCase.Name} marker mismatch.");

            var evidenceClient = reply.ProviderRid == "api-a" ? providerA : providerB;
            var evidence = (await evidenceClient.Post("/evidence/wait")
                .Body(new EvidenceWaitRequest(marker))
                .SubmitAsync<string[]>()).Body;
            ScenarioAssert.That(
                evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                    && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
                $"DR-A3 {registryCase.Name} provider evidence was not recorded.");
        }

        Console.WriteLine("scenario DR-A3 passed");
    }

    static ZLinkHttpClient CreateClient(string baseUrl) =>
        ZLinkHttpClient.Create(baseUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

    private sealed record RegistryCase(string Name, string RegistryUrl, string ConsumerUrl);
}
