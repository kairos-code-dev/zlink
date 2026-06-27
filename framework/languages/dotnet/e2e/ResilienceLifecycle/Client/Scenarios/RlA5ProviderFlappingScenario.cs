using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-A5 verifies provider repeatedly goes down and comes back while traffic converges.
internal static class RlA5ProviderFlappingScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
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

            for (var i = 0; i < 4; i++)
            {
                var marker = $"rl-a5-down-{cycle}-{i}";
                var reply = (await consumer.Post("/profile/request")
                    .Body(new ProfileRequest("fast", marker))
                    .SubmitAsync<ProfileReply>()).Body;
                ScenarioAssert.That(reply.ProviderRid == "api-a", "RL-A5 down window did not converge to api-a.");
            }

            await providerA.Post("/evidence/wait")
                .Body(new EvidenceWaitRequest([$"marker=rl-a5-down-{cycle}-"], []))
                .SubmitAsync<string[]>();

            await processes.StartProviderBAsync();
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
                    // The scenario keeps polling until api-b accepts HTTP traffic again.
                }

                await Task.Delay(100);
            }

            var sawApiB = false;
            for (var i = 0; i < 24; i++)
            {
                var marker = $"rl-a5-up-{cycle}-{i}";
                var reply = (await consumer.Post("/profile/request")
                    .Body(new ProfileRequest("fast", marker))
                    .SubmitAsync<ProfileReply>()).Body;
                ScenarioAssert.That(reply.Value == "profile:fast", "RL-A5 up-window request returned an unexpected value.");
                sawApiB = sawApiB || reply.ProviderRid == "api-b";
            }

            ScenarioAssert.That(sawApiB, "RL-A5 did not route traffic to api-b after restart.");

            await providerB.Post("/evidence/wait")
                .Body(new EvidenceWaitRequest([$"marker=rl-a5-up-{cycle}-"], []))
                .SubmitAsync<string[]>();
        }

        Console.WriteLine("scenario RL-A5 passed");
    }
}
