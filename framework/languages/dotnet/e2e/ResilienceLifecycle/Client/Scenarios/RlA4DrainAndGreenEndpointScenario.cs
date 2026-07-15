// Verifies RL-A4 Drain And Green Endpoint behavior.
using ResilienceLifecycle.Client.Support;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-A4 verifies drain followed by replacement with a green provider endpoint.
internal static class RlA4DrainAndGreenEndpointScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient registry,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerB)
    {
        await providerB.Post("/admin/drain").AsyncRaw();
        await WaitForWeightAsync(providerB, 0);

        var green = await processes.StartProviderBGreenAsync();
        using var greenProvider = ZLinkHttpClient.Create(green.Url)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        for (var i = 0; i < 12; i++)
        {
            var marker = $"rl-a4-rolling-{i}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileReq("fast", marker))
                .Async<ProfileRes>()).Body;
            ZlinkStreamAssert.Ensure(reply.Value == "profile:fast", "RL-A4 rolling request returned an unexpected value.");
            ZlinkStreamAssert.Ensure(reply.ProviderRid is "api-a" or "api-b",
                "RL-A4 rolling request used an unexpected provider.");
        }

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
            .Body(new TopologyWaitReq("api-b", "Ready", 1))
            .Async<TopologyEntryRes[]>();
        await ProviderTrafficProbe.DriveUntilProviderServesAsync(
            consumer, greenProvider, "rl-a4-green", "RL-A4");

        await greenProvider.Post("/shutdown").AsyncRaw();
        await WaitUntilUnavailableAsync(greenProvider);
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 0))
            .Async<TopologyEntryRes[]>();

        var restored = await processes.StartProviderBAsync();
        using var restoredProviderB = ZLinkHttpClient.Create(restored.Url)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await restoredProviderB.Get("/health").AsyncRaw();
                if (health.Status == 200) break;
            }
            catch
            {
                // The scenario keeps polling until the original provider accepts HTTP traffic.
            }

            await Task.Delay(100);
        }

        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 1))
            .Async<TopologyEntryRes[]>();
        await ProviderTrafficProbe.DriveUntilProviderServesAsync(
            consumer, restoredProviderB, "rl-a4-restored", "RL-A4");

        Console.WriteLine("scenario RL-A4 passed");
    }

    private static async Task WaitForWeightAsync(ZLinkHttpClient provider, int expected)
    {
        await provider.Post("/admin/weight/wait")
            .Body(new WeightWaitReq(expected))
            .AsyncRaw();
    }

    private static async Task WaitUntilUnavailableAsync(ZLinkHttpClient http)
    {
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await http.Get("/health").AsyncRaw();
                if (health.Status != 200) return;
            }
            catch
            {
                return;
            }

            await Task.Delay(100);
        }

        ZlinkStreamAssert.Ensure(false, "RL-A4 provider did not stop.");
    }
}
