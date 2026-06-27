using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-A3 verifies many reconnecting requests return to normal request flow.
internal static class RlA3ReconnectStormScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        foreach (var index in Enumerable.Range(0, 24))
        {
            var marker = $"rl-a3-{index}";
            var reply = (await consumer.Post("/profile/request/new-client")
                .Body(new ProfileRequest("fast", marker))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(
                reply.Value == "profile:fast" && reply.ProviderRid is "api-a" or "api-b",
                "RL-A3 storm request returned an unexpected reply.");
        }

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-a3-",
            "RL-A3 did not record expected provider evidence.");

        Console.WriteLine("scenario RL-A3 passed");
    }
}
