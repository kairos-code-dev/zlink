using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-D2 verifies dispatch observer fault isolation and continued messaging.
internal static class RlD2ObserverFaultScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerB.Post("/admin/fault/observer-throws").SubmitRawAsync();
        var missing = await consumer.Post("/profile/request/missing")
            .Body(new ProfileRequest("fast", "rl-d2-error"))
            .SubmitRawAsync();
        ScenarioAssert.That(missing.Status >= 500, "RL-D2 missing handler request should fail.");

        var followUp = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("fast", "rl-d2-after"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(followUp.Value == "profile:fast", "RL-D2 messaging did not continue after observer failure.");
        await providerB.Post("/admin/fault/none").SubmitRawAsync();

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-d2-after",
            "RL-D2 did not record expected evidence 'marker=rl-d2-after'.");

        Console.WriteLine("scenario RL-D2 passed");
    }
}
