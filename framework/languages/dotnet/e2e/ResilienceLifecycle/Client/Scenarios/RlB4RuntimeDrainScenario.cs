using ResilienceLifecycle.Shared;
using Zlink.HttpClient;
using ResilienceLifecycle.Client.Support;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-B4 verifies runtime drain and later restore for the provider path.
internal static class RlB4RuntimeDrainScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var beforeDrain = (await providerB.Get("/evidence").SubmitAsync<string[]>()).Body;
        await providerB.Post("/admin/drain").SubmitRawAsync();
        await WaitForWeightAsync(providerB, 0);

        for (var i = 0; i < 20; i++)
        {
            var marker = $"rl-b4-drained-{i}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", marker))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.ProviderRid == "api-a", "RL-B4 drained api-b received a new request.");
        }

        var afterDrain = (await providerB.Get("/evidence").SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            CountNew(afterDrain, beforeDrain, "profile-request|rid=api-b|marker=rl-b4-drained-") == 0,
            "RL-B4 api-b evidence changed after drain.");

        await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["profile-request|rid=api-a|marker=rl-b4-drained-"], []))
            .SubmitAsync<string[]>();

        await providerB.Post("/admin/restore").SubmitRawAsync();
        await WaitForWeightAsync(providerB, 100);

        for (var i = 0; i < 40; i++)
        {
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", $"rl-b4-restored-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-B4 restored request returned an unexpected value.");
        }

        await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["profile-request|rid=api-b|marker=rl-b4-restored-"], []))
            .SubmitAsync<string[]>();

        Console.WriteLine("scenario RL-B4 passed");
    }

    static async Task WaitForWeightAsync(ZLinkHttpClient provider, int expected)
    {
        await provider.Post("/admin/weight/wait")
            .Body(new WeightWaitRequest(expected))
            .SubmitRawAsync();
    }

    static int CountNew(string[] after, string[] before, string pattern) =>
        Math.Max(0, after.Count(line => line.Contains(pattern, StringComparison.Ordinal))
            - before.Count(line => line.Contains(pattern, StringComparison.Ordinal)));
}
