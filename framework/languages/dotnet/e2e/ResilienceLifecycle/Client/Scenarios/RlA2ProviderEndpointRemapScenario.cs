using ResilienceLifecycle.Client.Support;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-A2 verifies same provider rid comes back on a different endpoint.
internal static class RlA2ProviderEndpointRemapScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient registry,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerB)
    {
        await providerB.Post("/shutdown").SubmitRawAsync();
        await WaitUntilUnavailableAsync(providerB);

        var replacement = await processes.StartProviderBRemapAsync();
        using var replacementProvider = ZLinkHttpClient.Create(replacement.Url).Build();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 1))
            .SubmitAsync<TopologyEntryRes[]>();
        await SendRequestBatchAsync(consumer, "rl-a2-rescheduled");
        await replacementProvider.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(["marker=rl-a2-rescheduled-"], []))
            .SubmitAsync<string[]>();

        await replacementProvider.Post("/shutdown").SubmitRawAsync();
        await WaitUntilUnavailableAsync(replacementProvider);

        await processes.StartProviderBAsync();
        await WaitUntilAvailableAsync(providerB);
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 1))
            .SubmitAsync<TopologyEntryRes[]>();
        await SendRequestBatchAsync(consumer, "rl-a2-original-restored");
        await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(["marker=rl-a2-original-restored-"], []))
            .SubmitAsync<string[]>();

        Console.WriteLine("scenario RL-A2 passed");
    }

    private static async Task SendRequestBatchAsync(
        ZLinkHttpClient consumer,
        string markerPrefix)
    {
        for (var i = 0; i < 32; i++)
        {
            var marker = $"{markerPrefix}-{i}";
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileReq("fast", marker))
                .SubmitAsync<ProfileRes>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-A2 request returned an unexpected value.");
        }
    }

    private static async Task WaitUntilAvailableAsync(ZLinkHttpClient http)
    {
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await http.Get("/health").SubmitRawAsync();
                if (health.Status == 200) return;
            }
            catch
            {
            }

            await Task.Delay(100);
        }

        ScenarioAssert.That(false, "RL-A2 provider did not become healthy.");
    }

    private static async Task WaitUntilUnavailableAsync(ZLinkHttpClient http)
    {
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await http.Get("/health").SubmitRawAsync();
                if (health.Status != 200) return;
            }
            catch
            {
                return;
            }

            await Task.Delay(100);
        }

        ScenarioAssert.That(false, "RL-A2 provider did not stop.");
    }
}