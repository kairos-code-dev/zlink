using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-B6 verifies mixed gray failures, healthy-provider success, and recovery.
internal static class RlB6GrayFaultScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerB.Post("/admin/fault/gray").SubmitRawAsync();

        var failures = 0;
        var successes = 0;
        for (var i = 0; i < 30; i++)
        {
            try
            {
                var reply = (await consumer.Post("/profile/request")
                    .Body(new ProfileRequest(i % 3 == 0 ? "gray" : "fast", $"rl-b6-{i}"))
                    .SubmitAsync<ProfileReply>()).Body;
                if (reply.ProviderRid == "api-a")
                {
                    successes++;
                }
            }
            catch
            {
                failures++;
            }
        }

        ScenarioAssert.That(successes > 0 && failures > 0, "RL-B6 expected both healthy successes and gray failures.");
        await providerB.Post("/admin/fault/none").SubmitRawAsync();
        var followUp = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("fast", "rl-b6-after"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(followUp.Value == "profile:fast", "RL-B6 follow-up request failed after clearing fault.");

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-b6-",
            "RL-B6 did not record expected evidence 'marker=rl-b6-'.");
        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-b6-after",
            "RL-B6 did not record expected evidence 'marker=rl-b6-after'.");

        Console.WriteLine("scenario RL-B6 passed");
    }
}
