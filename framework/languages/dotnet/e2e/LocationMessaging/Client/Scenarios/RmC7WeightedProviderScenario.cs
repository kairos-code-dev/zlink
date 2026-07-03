using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-C7 verifies that providers advertising different build-time weights via
// their peer location rows send
// distinctly more requests to the higher-weight provider.
internal static class RmC7WeightedProviderScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await using var cluster = await DynamicClusterLauncher.StartAsync(options, "rm-c7");
        var providerA = await cluster.StartProviderAsync("api-a-weighted", "api-a", 75);
        var providerB = await cluster.StartProviderAsync("api-b-weighted", "api-b", 25);
        using var requester = ZLinkHttpClient.Create(providerA.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();
        using var providerBClient = ZLinkHttpClient.Create(providerB.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        var beforeA = await ReadEvidenceAsync(requester);
        var beforeB = await ReadEvidenceAsync(providerBClient);
        var marker = $"rm-c7-{Guid.NewGuid():N}";
        var values = Enumerable.Range(0, 240)
            .Select(index => $"{marker}-{index}")
            .ToArray();
        var replies = new List<ProfileRes>(values.Length);
        foreach (var value in values)
        {
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileReq(value))
                .SubmitAsync<ProfileRes>()).Body;
            replies.Add(reply);
        }

        ScenarioAssert.That(replies.Count == values.Length, "RM-C7 reply count mismatch.");
        ScenarioAssert.That(
            replies.All(reply => reply.ProviderRid is "api-a" or "api-b"),
            "RM-C7 reply provider mismatch.");

        var apiAValues = replies
            .Where(reply => reply.ProviderRid == "api-a")
            .Select(reply => reply.Value)
            .ToArray();
        var apiBValues = replies
            .Where(reply => reply.ProviderRid == "api-b")
            .Select(reply => reply.Value)
            .ToArray();
        ScenarioAssert.That(apiAValues.Length > 0 && apiBValues.Length > 0, "RM-C7 expected both weighted providers.");
        var afterA = await WaitEvidenceAsync(requester, apiAValues[^1]);
        var afterB = await WaitEvidenceAsync(providerBClient, apiBValues[^1]);
        var counts = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            ["apiA"] = ScenarioAssert.CountNewEvidence(afterA, beforeA, "profile-request|rid=api-a", marker),
            ["apiB"] = ScenarioAssert.CountNewEvidence(afterB, beforeB, "profile-request|rid=api-b", marker)
        };
        ScenarioAssert.That(
            counts["apiA"] == apiAValues.Length
            && counts["apiB"] == apiBValues.Length
            && counts["apiA"] + counts["apiB"] == values.Length
            && counts["apiA"] > counts["apiB"] * 2,
            "RM-C7 weighted provider counts did not favor api-a.");

        Console.WriteLine("scenario RM-C7 passed");
    }

    private static async Task<string[]> ReadEvidenceAsync(ZLinkHttpClient http)
    {
        return (await http.Get("/evidence").SubmitAsync<string[]>()).Body;
    }

    private static async Task<string[]> WaitEvidenceAsync(ZLinkHttpClient http, string contains)
    {
        return (await http.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(contains, 20000))
            .SubmitAsync<string[]>()).Body;
    }
}