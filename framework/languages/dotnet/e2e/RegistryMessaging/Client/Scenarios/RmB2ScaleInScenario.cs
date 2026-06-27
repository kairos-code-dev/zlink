using Zlink.HttpClient;
using RegistryMessaging.Client;
using RegistryMessaging.Shared;

namespace RegistryMessaging.Client.Scenarios;

// RM-B2 verifies that traffic continues through the remaining provider after
// one provider is stopped for scale-in.
internal static class RmB2ScaleInScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await using var cluster = await DynamicClusterLauncher.StartAsync(options, "rm-b2");
        var providerA = await cluster.StartProviderAsync("api-a", "api-a");
        var providerB = await cluster.StartProviderAsync("api-b", "api-b");
        using var requester = ZLinkHttpClient.Create(providerA.HttpUrl)
            .Json()
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();
        using var providerBClient = ZLinkHttpClient.Create(providerB.HttpUrl)
            .Json()
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        var beforeA = await ReadEvidenceAsync(requester);
        var beforeB = await ReadEvidenceAsync(providerBClient);
        var markerBefore = $"rm-b2-before-{Guid.NewGuid():N}";
        var valuesBefore = Enumerable.Range(0, 40)
            .Select(index => $"{markerBefore}-{index}")
            .ToArray();
        var repliesBefore = new List<ProfileReply>(valuesBefore.Length);
        foreach (var value in valuesBefore)
        {
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileRequest(value))
                .SubmitAsync<ProfileReply>()).Body;
            repliesBefore.Add(reply);
        }

        ScenarioAssert.That(repliesBefore.Count == valuesBefore.Length, "RM-B2 pre-scale reply count mismatch.");
        ScenarioAssert.That(
            repliesBefore.All(reply => reply.ProviderRid is "api-a" or "api-b"),
            "RM-B2 reply provider mismatch before scale-in.");

        var apiABeforeValues = repliesBefore
            .Where(reply => reply.ProviderRid == "api-a")
            .Select(reply => reply.Value)
            .ToArray();
        var apiBBeforeValues = repliesBefore
            .Where(reply => reply.ProviderRid == "api-b")
            .Select(reply => reply.Value)
            .ToArray();
        ScenarioAssert.That(apiABeforeValues.Length > 0 && apiBBeforeValues.Length > 0,
            "RM-B2 expected both providers before scale-in.");
        var scaleOutA = await WaitEvidenceAsync(requester, apiABeforeValues[^1]);
        var scaleOutB = await WaitEvidenceAsync(providerBClient, apiBBeforeValues[^1]);
        var preA = ScenarioAssert.CountNewEvidence(scaleOutA, beforeA, "profile-request|rid=api-a", markerBefore);
        var preB = ScenarioAssert.CountNewEvidence(scaleOutB, beforeB, "profile-request|rid=api-b", markerBefore);
        ScenarioAssert.That(preA == apiABeforeValues.Length && preB == apiBBeforeValues.Length
            && preA + preB == valuesBefore.Length, "RM-B2 expected both providers before scale-in.");

        await cluster.StopAsync(providerB);
        await Task.Delay(TimeSpan.FromSeconds(1));

        beforeA = await ReadEvidenceAsync(requester);
        beforeB = await ReadEvidenceIgnoringStoppedAsync(providerBClient);
        var markerAfter = $"rm-b2-after-{Guid.NewGuid():N}";
        var valuesAfter = Enumerable.Range(0, 20)
            .Select(index => $"{markerAfter}-{index}")
            .ToArray();
        var repliesAfter = new List<ProfileReply>(valuesAfter.Length);
        foreach (var value in valuesAfter)
        {
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileRequest(value))
                .SubmitAsync<ProfileReply>()).Body;
            repliesAfter.Add(reply);
        }

        ScenarioAssert.That(repliesAfter.Count == valuesAfter.Length, "RM-B2 post-scale reply count mismatch.");
        ScenarioAssert.That(
            repliesAfter.All(reply => reply.ProviderRid == "api-a"),
            "RM-B2 after scale-in should reach api-a only.");

        var afterA = await WaitEvidenceAsync(requester, valuesAfter[^1]);
        var afterB = await ReadEvidenceIgnoringStoppedAsync(providerBClient);
        var a = ScenarioAssert.CountNewEvidence(afterA, beforeA, "profile-request|rid=api-a", markerAfter);
        var b = ScenarioAssert.CountNewEvidence(afterB, beforeB, "profile-request|rid=api-b", markerAfter);
        ScenarioAssert.That(a == valuesAfter.Length && b == 0, "RM-B2 expected only api-a after scale-in.");

        Console.WriteLine("scenario RM-B2 passed");
    }

    static async Task<string[]> ReadEvidenceAsync(ZLinkHttpClient http) =>
        (await http.Get("/evidence").SubmitAsync<string[]>()).Body;

    static async Task<string[]> ReadEvidenceIgnoringStoppedAsync(ZLinkHttpClient http)
    {
        try
        {
            return await ReadEvidenceAsync(http);
        }
        catch
        {
            return [];
        }
    }

    static async Task<string[]> WaitEvidenceAsync(ZLinkHttpClient http, string contains) =>
        (await http.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(contains))
            .SubmitAsync<string[]>()).Body;
}
