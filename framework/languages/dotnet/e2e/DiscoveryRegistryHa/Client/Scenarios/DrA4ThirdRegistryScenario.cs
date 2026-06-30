using DiscoveryRegistryHa.Client.Support;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-A4 observes same-rid advertisements on different endpoints and verifies
// the consumer still reaches a live api-a provider without hanging.
internal static class DrA4ThirdRegistryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var consumer = ZLinkHttpClient.Create(options.Reg2ConsumerUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerA = ZLinkHttpClient.Create(options.ProviderAUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var duplicateProvider = ZLinkHttpClient.Create(options.DuplicateProviderUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        var marker = $"dr-a4-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request/wait")
            .Body(new ProfileReq("dr-a4", marker))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-a4", "DR-A4 request failed.");
        ScenarioAssert.That(reply.ProviderRid == "api-a", "DR-A4 same-rid providers should report api-a.");
        ScenarioAssert.That(reply.Marker == marker, "DR-A4 marker mismatch.");

        var evidence = await WaitForEitherProviderEvidenceAsync(providerA, duplicateProvider, marker);
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(marker, StringComparison.Ordinal)
                                 && line.Contains("rid=api-a", StringComparison.Ordinal)),
            "DR-A4 provider evidence was not recorded.");

        Console.WriteLine("scenario DR-A4 passed");
    }

    private static async Task<string[]> WaitForEitherProviderEvidenceAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient duplicateProvider,
        string marker)
    {
        var waitA = WaitForEvidenceAsync(providerA, marker);
        var waitB = WaitForEvidenceAsync(duplicateProvider, marker);
        var completed = await Task.WhenAny(waitA, waitB);
        return await completed;
    }

    private static async Task<string[]> WaitForEvidenceAsync(ZLinkHttpClient client, string marker)
    {
        return (await client.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(marker))
            .SubmitAsync<string[]>()).Body;
    }
}