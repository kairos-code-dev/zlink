using DiscoveryRegistryHa.Client.Support;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-B1 verifies late-start registries catch up through peer broadcast and can
// route consumer requests without restarting providers or consumers.
internal static class DrB1FailoverScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        var cases = new[]
        {
            new RegistryCase("reg-2", options.Reg2Url, options.Reg2ConsumerUrl),
            new RegistryCase("reg-3", options.Reg3Url, options.Reg3ConsumerUrl)
        };

        using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerB = ZLinkHttpClient.Create(options.ProviderBUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        foreach (var registryCase in cases)
        {
            using var registry = ZLinkHttpClient.Create(registryCase.RegistryUrl)
                .Timeout(TimeSpan.FromSeconds(10))
                .Build();
            using var consumer = ZLinkHttpClient.Create(registryCase.ConsumerUrl)
                .Timeout(TimeSpan.FromSeconds(10))
                .Build();

            await registry.Post("/registry/members/wait")
                .Body(new MemberEndpointWaitReq(options.ApiAEndpoint))
                .SubmitRawAsync();
            await registry.Post("/registry/members/wait")
                .Body(new MemberEndpointWaitReq(options.ApiBEndpoint))
                .SubmitRawAsync();

            var marker = $"dr-b1-{registryCase.Name}-{Guid.NewGuid():N}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileReq("dr-b1", marker))
                .SubmitAsync<ProfileRes>()).Body;
            ScenarioAssert.That(reply.Value == "profile:dr-b1", $"DR-B1 {registryCase.Name} request failed.");
            ScenarioAssert.That(
                reply.ProviderRid is "api-a" or "api-b",
                $"DR-B1 {registryCase.Name} routed to an unexpected provider.");
            ScenarioAssert.That(reply.Marker == marker, $"DR-B1 {registryCase.Name} marker mismatch.");

            var evidenceClient = reply.ProviderRid == "api-a" ? providerA : providerB;
            var evidence = (await evidenceClient.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(marker))
                .SubmitAsync<string[]>()).Body;
            ScenarioAssert.That(
                evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                                     && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
                $"DR-B1 {registryCase.Name} provider evidence was not recorded.");
        }

        Console.WriteLine("scenario DR-B1 passed");
    }

    private sealed record RegistryCase(string Name, string RegistryUrl, string ConsumerUrl);
}