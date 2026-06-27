using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-C3 verifies node pause/recovery behavior through provider restart.
internal static class RlC3NodePauseRecoveryScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient registry,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerB.Post("/shutdown").SubmitRawAsync();
        await WaitUntilAsync(async () => !await IsHealthyAsync(providerB), "RL-C3 expected api-b simulated node pause/down.");

        var during = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("fast", "rl-c3-during-down"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(during.ProviderRid == "api-a", "RL-C3 did not use surviving provider during node down.");

        await processes.StartProviderBAsync();
        var recovered = false;
        for (var i = 0; i < 40; i++)
        {
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", $"rl-c3-recovered-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
            recovered = recovered || reply.ProviderRid == "api-b";
            if (recovered)
            {
                break;
            }

            await Task.Delay(100);
        }

        ScenarioAssert.That(recovered, "RL-C3 api-b did not recover.");

        await registry.Post("/topology/wait")
            .Body(new TopologyWaitRequest("api-b", "Ready", 1))
            .SubmitAsync<TopologyEntryResult[]>();

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-c3-during-down",
            "RL-C3 did not record expected evidence 'marker=rl-c3-during-down'.");
        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            "marker=rl-c3-recovered-",
            "RL-C3 did not record expected evidence 'marker=rl-c3-recovered-'.");

        Console.WriteLine("scenario RL-C3 passed");
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
