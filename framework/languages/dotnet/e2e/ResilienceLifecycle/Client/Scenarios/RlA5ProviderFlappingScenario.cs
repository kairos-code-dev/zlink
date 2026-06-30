using ResilienceLifecycle.Client.Support;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-A5 verifies provider repeatedly goes down and comes back while traffic converges.
internal static class RlA5ProviderFlappingScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient registry,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        for (var cycle = 0; cycle < 3; cycle++)
        {
            await providerB.Post("/shutdown").SubmitRawAsync();
            for (var attempt = 0; attempt < 100; attempt++)
            {
                try
                {
                    var health = await providerB.Get("/health").SubmitRawAsync();
                    if (health.Status != 200) break;
                }
                catch
                {
                    break;
                }

                await Task.Delay(100);
            }

            for (var i = 0; i < 4; i++)
            {
                var marker = $"rl-a5-down-{cycle}-{i}";
                var reply = (await consumer.Post("/profile/request")
                    .Body(new ProfileReq("fast", marker))
                    .SubmitAsync<ProfileRes>()).Body;
                ScenarioAssert.That(reply.ProviderRid == "api-a", "RL-A5 down window did not converge to api-a.");
            }

            await providerA.Post("/evidence/wait")
                .Body(new EvidenceWaitReq([$"marker=rl-a5-down-{cycle}-"], []))
                .SubmitAsync<string[]>();

            await processes.StartProviderBAsync();
            for (var attempt = 0; attempt < 100; attempt++)
            {
                try
                {
                    var health = await providerB.Get("/health").SubmitRawAsync();
                    if (health.Status == 200) break;
                }
                catch
                {
                    // The scenario keeps polling until api-b accepts HTTP traffic again.
                }

                await Task.Delay(100);
            }

            await registry.Post("/topology/wait")
                .Body(new TopologyWaitReq("api-b", "Ready", 1))
                .SubmitAsync<TopologyEntryRes[]>();
            for (var i = 0; i < 24; i++)
            {
                var marker = $"rl-a5-up-{cycle}-{i}";
                var reply = (await consumer.Post("/profile/request")
                    .Body(new ProfileReq("fast", marker))
                    .SubmitAsync<ProfileRes>()).Body;
                ScenarioAssert.That(reply.Value == "profile:fast",
                    "RL-A5 up-window request returned an unexpected value.");
            }

            await providerB.Post("/evidence/wait")
                .Body(new EvidenceWaitReq([$"profile-request|rid=api-b|marker=rl-a5-up-{cycle}-"], []))
                .SubmitAsync<string[]>();
        }

        Console.WriteLine("scenario RL-A5 passed");
    }
}