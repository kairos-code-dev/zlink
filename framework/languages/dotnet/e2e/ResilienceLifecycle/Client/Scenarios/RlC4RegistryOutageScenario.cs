using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-C4 verifies established direct traffic during registry outage and later discovery recovery.
internal static class RlC4RegistryOutageScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient registry,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var before = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("fast", "rl-c4-before-outage"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(before.Value == "profile:fast", "RL-C4 request failed before registry outage.");

        await registry.Post("/shutdown").SubmitRawAsync();
        await processes.WaitRegistryHealthAsync(expectedHealthy: false, TimeSpan.FromSeconds(30));

        var during = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("fast", "rl-c4-during-outage"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(during.Value == "profile:fast", "RL-C4 existing channel failed during registry outage.");

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-c4-before-outage",
            "RL-C4 did not record expected evidence 'marker=rl-c4-before-outage'.");
        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-c4-during-outage",
            "RL-C4 did not record expected evidence 'marker=rl-c4-during-outage'.");

        await processes.StartRegistryAsync();
        await providerA.Post("/shutdown").SubmitRawAsync();
        await WaitUntilAsync(async () => !await IsHealthyAsync(providerA), "RL-C4 expected api-a restart after registry recovery.");
        await processes.StartProviderAAsync();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitRequest("api-a", "Ready", 1))
            .SubmitAsync<TopologyEntryResult[]>();

        var after = (await consumer.Post("/profile/request/new-client")
            .Body(new ProfileRequest("fast", "rl-c4-after-restart"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(after.Value == "profile:fast", "RL-C4 follow-up request failed after registry restart.");

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-c4-after-restart",
            "RL-C4 did not record expected evidence 'marker=rl-c4-after-restart'.");

        Console.WriteLine("scenario RL-C4 passed");
    }

    static async Task<bool> IsHealthyAsync(ZLinkHttpClient provider)
    {
        try
        {
            return (await provider.Get("/health").SubmitRawAsync()).Status == 200;
        }
        catch
        {
            return false;
        }
    }

    static async Task WaitUntilAsync(Func<Task<bool>> condition, string message)
    {
        for (var attempt = 0; attempt < 120; attempt++)
        {
            if (await condition())
            {
                return;
            }

            await Task.Delay(250);
        }

        throw new InvalidOperationException(message);
    }
}
