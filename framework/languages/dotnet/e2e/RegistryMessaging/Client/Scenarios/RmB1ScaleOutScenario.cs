using RegistryMessaging.Client.Support;
using RegistryMessaging.Shared;
using Zlink.HttpClient;

namespace RegistryMessaging.Client.Scenarios;

// RM-B1 verifies that a newly started provider is discovered and receives
// profile traffic after scale-out.
internal static class RmB1ScaleOutScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await using var cluster = await DynamicClusterLauncher.StartAsync(options, "rm-b1");
        var providerA = await cluster.StartProviderAsync("api-a", "api-a");
        using var requester = ZLinkHttpClient.Create(providerA.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        var beforeA = await ReadEvidenceAsync(requester);
        var markerBefore = $"rm-b1-before-{Guid.NewGuid():N}";
        for (var i = 0; i < 10; i++)
        {
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileRequest($"{markerBefore}-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.ProviderRid == "api-a", "RM-B1 before scale-out should reach api-a.");
        }

        var preScaleEvidence = await WaitEvidenceAsync(requester, $"{markerBefore}-9");
        ScenarioAssert.That(
            ScenarioAssert.CountNewEvidence(
                preScaleEvidence,
                beforeA,
                "profile-request|rid=api-a",
                markerBefore) == 10,
            "RM-B1 pre-scale evidence was not api-a only.");

        var providerB = await cluster.StartProviderAsync("api-b", "api-b");
        using var providerBClient = ZLinkHttpClient.Create(providerB.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        beforeA = await ReadEvidenceAsync(requester);
        var beforeB = await ReadEvidenceAsync(providerBClient);
        var markerAfter = $"rm-b1-after-{Guid.NewGuid():N}";
        var values = Enumerable.Range(0, 60)
            .Select(index => $"{markerAfter}-{index}")
            .ToArray();
        var replies = new List<ProfileReply>(values.Length);
        foreach (var value in values)
        {
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileRequest(value))
                .SubmitAsync<ProfileReply>()).Body;
            replies.Add(reply);
        }

        ScenarioAssert.That(replies.Count == values.Length, "RM-B1 scale-out reply count mismatch.");
        foreach (var reply in replies)
            ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b", "RM-B1 reply provider mismatch.");

        var apiAValues = replies
            .Where(reply => reply.ProviderRid == "api-a")
            .Select(reply => reply.Value)
            .ToArray();
        var apiBValues = replies
            .Where(reply => reply.ProviderRid == "api-b")
            .Select(reply => reply.Value)
            .ToArray();
        ScenarioAssert.That(apiAValues.Length > 0 && apiBValues.Length > 0,
            "RM-B1 expected both providers after scale-out.");

        var afterA = await WaitEvidenceAsync(requester, apiAValues[^1]);
        var afterB = await WaitEvidenceAsync(providerBClient, apiBValues[^1]);
        var a = ScenarioAssert.CountNewEvidence(afterA, beforeA, "profile-request|rid=api-a", markerAfter);
        var b = ScenarioAssert.CountNewEvidence(afterB, beforeB, "profile-request|rid=api-b", markerAfter);
        ScenarioAssert.That(a == apiAValues.Length && b == apiBValues.Length && a + b == values.Length,
            "RM-B1 expected evidence to match scale-out replies.");

        Console.WriteLine("scenario RM-B1 passed");
    }

    private static async Task<string[]> ReadEvidenceAsync(ZLinkHttpClient http)
    {
        return (await http.Get("/evidence").SubmitAsync<string[]>()).Body;
    }

    private static async Task<string[]> WaitEvidenceAsync(ZLinkHttpClient http, string contains)
    {
        return (await http.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(contains))
            .SubmitAsync<string[]>()).Body;
    }
}