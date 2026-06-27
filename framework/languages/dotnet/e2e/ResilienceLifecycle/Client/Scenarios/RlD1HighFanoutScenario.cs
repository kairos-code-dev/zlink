using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-D1 verifies a high-fanout request burst over the channel.
internal static class RlD1HighFanoutScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var tasks = Enumerable.Range(0, 120).Select(async i =>
        {
            return (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", $"rl-d1-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
        });
        var replies = await Task.WhenAll(tasks);
        ScenarioAssert.That(
            replies.Length == 120 && replies.All(reply => reply.Value == "profile:fast"),
            "RL-D1 high request fanout did not complete.");

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-d1-",
            "RL-D1 did not record expected evidence 'marker=rl-d1-'.");

        Console.WriteLine("scenario RL-D1 passed");
    }
}
