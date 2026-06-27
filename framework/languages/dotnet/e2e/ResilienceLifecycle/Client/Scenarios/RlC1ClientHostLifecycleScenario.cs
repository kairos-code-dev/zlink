using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-C1 verifies repeated client lifecycle traffic and a follow-up request.
internal static class RlC1ClientHostLifecycleScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        foreach (var index in Enumerable.Range(0, 12))
        {
            var reply = (await consumer.Post("/profile/request/new-client")
                .Body(new ProfileRequest("fast", $"rl-c1-{index}"))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(
                reply.Value == "profile:fast",
                "RL-C1 request failed before cleanup.");
        }

        var followUp = (await consumer.Post("/profile/request/new-client")
            .Body(new ProfileRequest("fast", "rl-c1-after-cleanup"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(followUp.Value == "profile:fast", "RL-C1 follow-up failed after client cleanup.");

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-c1-",
            "RL-C1 did not record expected evidence 'marker=rl-c1-'.");
        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-c1-after-cleanup",
            "RL-C1 did not record expected evidence 'marker=rl-c1-after-cleanup'.");

        Console.WriteLine("scenario RL-C1 passed");
    }
}
