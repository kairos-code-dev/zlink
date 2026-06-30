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
        await providerB.Post("/admin/drain").SubmitRawAsync();
        await WaitForWeightAsync(providerB, 0);

        var green = await processes.StartProviderBGreenAsync();
        using var greenProvider = ZLinkHttpClient.Create(green.Url).Json().Build();

        for (var i = 0; i < 12; i++)
        {
            var marker = $"rl-a4-rolling-{i}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", marker))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-A4 rolling request returned an unexpected value.");
            ScenarioAssert.That(reply.ProviderRid is "api-a" or "api-b",
                "RL-A4 rolling request used an unexpected provider.");
        }

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

        await registry.Post("/topology/wait")
            .Body(new TopologyWaitRequest("api-b", "Ready", 1))
            .SubmitAsync<TopologyEntryResult[]>();
        for (var i = 0; i < 32; i++)
        {
            var marker = $"rl-a4-green-{i}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", marker))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-A4 green request returned an unexpected value.");
        }

        await greenProvider.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["marker=rl-a4-green-"], []))
            .SubmitAsync<string[]>();

        await greenProvider.Post("/shutdown").SubmitRawAsync();
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
                // The scenario keeps polling until the original provider accepts HTTP traffic.
            }

            await Task.Delay(100);
        }

        await registry.Post("/topology/wait")
            .Body(new TopologyWaitRequest("api-b", "Ready", 1))
            .SubmitAsync<TopologyEntryResult[]>();
        for (var i = 0; i < 32; i++)
        {
            var marker = $"rl-a4-restored-{i}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", marker))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-A4 restored request returned an unexpected value.");
        }

        await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["marker=rl-a4-restored-"], []))
            .SubmitAsync<string[]>();

        Console.WriteLine("scenario RL-A4 passed");
    }

    private static async Task WaitForWeightAsync(ZLinkHttpClient provider, int expected)
    {
        await provider.Post("/admin/weight/wait")
            .Body(new WeightWaitRequest(expected))
            .SubmitRawAsync();
    }
}