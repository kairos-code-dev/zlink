using ResilienceLifecycle.Client.Support;
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
        await WaitUntilAsync(async () => !await IsHealthyAsync(providerB),
            "RL-C3 expected api-b simulated node pause/down.");

        var during = (await consumer.Post("/profile/request")
            .Body(new ProfileReq("fast", "rl-c3-during-down"))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(during.ProviderRid == "api-a", "RL-C3 did not use surviving provider during node down.");

        await processes.StartProviderBAsync();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 1))
            .SubmitAsync<TopologyEntryRes[]>();
        for (var i = 0; i < 40; i++)
        {
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileReq("fast", $"rl-c3-recovered-{i}"))
                .SubmitAsync<ProfileRes>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-C3 recovered request returned an unexpected value.");
        }

        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var waitA = providerA.Post("/evidence/wait").Body(new EvidenceWaitReq(["marker=rl-c3-during-down"], []))
                .SubmitAsync<string[]>(timeout.Token).AsTask();
            var waitB = providerB.Post("/evidence/wait").Body(new EvidenceWaitReq(["marker=rl-c3-during-down"], []))
                .SubmitAsync<string[]>(timeout.Token).AsTask();
            var completed = await Task.WhenAny(waitA, waitB);
            var evidence = (await completed).Body;
            timeout.Cancel();
            ScenarioAssert.That(
                evidence.Any(line => line.Contains("marker=rl-c3-during-down", StringComparison.Ordinal)),
                "RL-C3 did not record expected evidence 'marker=rl-c3-during-down'.");
        }
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var waitA = providerA.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(["profile-request|rid=api-b|marker=rl-c3-recovered-"], []))
                .SubmitAsync<string[]>(timeout.Token).AsTask();
            var waitB = providerB.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(["profile-request|rid=api-b|marker=rl-c3-recovered-"], []))
                .SubmitAsync<string[]>(timeout.Token).AsTask();
            var completed = await Task.WhenAny(waitA, waitB);
            var evidence = (await completed).Body;
            timeout.Cancel();
            ScenarioAssert.That(
                evidence.Any(line =>
                    line.Contains("profile-request|rid=api-b|marker=rl-c3-recovered-", StringComparison.Ordinal)),
                "RL-C3 did not record expected evidence 'marker=rl-c3-recovered-'.");
        }

        Console.WriteLine("scenario RL-C3 passed");
    }

    private static async Task<bool> IsHealthyAsync(ZLinkHttpClient provider)
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

    private static async Task WaitUntilAsync(Func<Task<bool>> condition, string message)
    {
        for (var attempt = 0; attempt < 120; attempt++)
        {
            if (await condition()) return;

            await Task.Delay(250);
        }

        throw new InvalidOperationException(message);
    }
}