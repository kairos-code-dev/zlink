// Verifies RL-A5 Provider Flapping behavior.
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
            if (!await processes.TryStopProviderBAsync())
            {
                await providerB.Post("/shutdown").AsyncRaw();
                for (var attempt = 0; attempt < 100; attempt++)
                {
                    try
                    {
                        var health = await providerB.Get("/health").AsyncRaw();
                        if (health.Status != 200) break;
                    }
                    catch
                    {
                        break;
                    }

                    await Task.Delay(100);
                }
            }

            await registry.Post("/topology/wait")
                .Body(new TopologyWaitReq("api-b", "Ready", 0))
                .Async<TopologyEntryRes[]>();
            await ProviderTrafficProbe.WaitUntilProviderExcludedAsync(
                consumer,
                "api-b",
                $"rl-a5-converge-{cycle}",
                "RL-A5");

            var downMarker = $"rl-a5-down-{cycle}";
            var downReply = (await consumer.Post("/profile/request")
                .Body(new ProfileReq("fast", downMarker))
                .Async<ProfileRes>()).Body;
            ZlinkStreamAssert.Ensure(downReply.ProviderRid == "api-a",
                "RL-A5 first request after observed convergence did not use api-a.");
            await providerA.Post("/evidence/wait")
                .Body(new EvidenceWaitReq([$"marker={downMarker}"], []))
                .Async<string[]>();

            var restarted = await processes.StartProviderBAsync();
            using var restartedProviderB = ZLinkHttpClient.Create(restarted.Url)
                .Timeout(TimeSpan.FromMinutes(5))
                .Build();
            for (var attempt = 0; attempt < 100; attempt++)
            {
                try
                {
                    var health = await restartedProviderB.Get("/health").AsyncRaw();
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
                .Async<TopologyEntryRes[]>();
            await ProviderTrafficProbe.DriveUntilProviderServesAsync(
                consumer,
                restartedProviderB,
                $"rl-a5-up-{cycle}",
                "RL-A5",
                $"profile-request|rid=api-b|marker=rl-a5-up-{cycle}-");
        }

        Console.WriteLine("scenario RL-A5 passed");
    }
}
