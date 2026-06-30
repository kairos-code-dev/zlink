using ResilienceLifecycle.Client.Support;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

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
                .Body(new ProfileReq("fast", marker))
                .SubmitAsync<ProfileRes>()).Body;
            ScenarioAssert.That(reply.ProviderRid == "api-a", "RL-B4 drained api-b received a new request.");
        }

        var afterDrain = (await providerB.Get("/evidence").SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            CountNew(afterDrain, beforeDrain, "profile-request|rid=api-b|marker=rl-b4-drained-") == 0,
            "RL-B4 api-b evidence changed after drain.");

        await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(["profile-request|rid=api-a|marker=rl-b4-drained-"], []))
            .SubmitAsync<string[]>();

        await providerB.Post("/admin/restore").SubmitRawAsync();
        await WaitForWeightAsync(providerB, 100);

        for (var i = 0; i < 40; i++)
        {
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileReq("fast", $"rl-b4-restored-{i}"))
                .SubmitAsync<ProfileRes>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-B4 restored request returned an unexpected value.");
        }

        await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(["profile-request|rid=api-b|marker=rl-b4-restored-"], []))
            .SubmitAsync<string[]>();

        Console.WriteLine("scenario RL-B4 passed");
    }

    private static async Task WaitForWeightAsync(ZLinkHttpClient provider, int expected)
    {
        await provider.Post("/admin/weight/wait")
            .Body(new WeightWaitReq(expected))
            .SubmitRawAsync();
    }

    private static int CountNew(string[] after, string[] before, string pattern)
    {
        return Math.Max(0, after.Count(line => line.Contains(pattern, StringComparison.Ordinal))
                           - before.Count(line => line.Contains(pattern, StringComparison.Ordinal)));
    }
}