using ResilienceLifecycle.Client.Support;
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

        await processes.WaitInitialProviderBExitedAsync();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 0))
            .Async<TopologyEntryRes[]>();

        for (var i = 0; i < 12; i++)
        {
            var marker = $"rl-a1-down-{i}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileReq("fast", marker))
                .Async<ProfileRes>()).Body;
            ZlinkStreamAssert.Ensure(reply.ProviderRid == "api-a",
                "RL-A1 request during api-b restart did not use surviving provider.");
        }

        await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(["marker=rl-a1-down-"], []))
            .Async<string[]>();

        await processes.StartProviderBAsync();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 1))
            .Async<TopologyEntryRes[]>();
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await providerB.Get("/health").AsyncRaw();
                if (health.Status == 200) break;
            }
            catch
            {
                // The scenario keeps polling until the restarted provider accepts HTTP traffic.
            }

            await Task.Delay(100);
        }

        await ProviderTrafficProbe.DriveUntilProviderServesAsync(
            consumer, providerB, "rl-a1-restored", "RL-A1");

        Console.WriteLine("scenario RL-A1 passed");
    }
}
