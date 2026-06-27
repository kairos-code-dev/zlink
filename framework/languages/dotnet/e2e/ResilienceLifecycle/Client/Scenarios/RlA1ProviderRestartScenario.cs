using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-A1 verifies recovery when the same provider endpoint is restarted.
internal static class RlA1ProviderRestartScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient registry,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerB.Post("/shutdown").SubmitRawAsync();
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await providerB.Get("/health").SubmitRawAsync();
                if (health.Status != 200)
                {
                    break;
                }
            }
            catch
            {
                break;
            }

            await Task.Delay(100);
        }

        for (var i = 0; i < 12; i++)
        {
            var marker = $"rl-a1-down-{i}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", marker))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.ProviderRid == "api-a", "RL-A1 request during api-b restart did not use surviving provider.");
        }

        await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["marker=rl-a1-down-"], []))
            .SubmitAsync<string[]>();

        await processes.StartProviderBAsync();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitRequest("api-b", "Ready", 1))
            .SubmitAsync<TopologyEntryResult[]>();
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await providerB.Get("/health").SubmitRawAsync();
                if (health.Status == 200)
                {
                    break;
                }
            }
            catch
            {
                // The scenario keeps polling until the restarted provider accepts HTTP traffic.
            }

            await Task.Delay(100);
        }

        for (var i = 0; i < 32; i++)
        {
            var marker = $"rl-a1-restored-{i}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", marker))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-A1 restored request returned an unexpected value.");
        }

        await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["marker=rl-a1-restored-"], []))
            .SubmitAsync<string[]>();

        Console.WriteLine("scenario RL-A1 passed");
    }
}
