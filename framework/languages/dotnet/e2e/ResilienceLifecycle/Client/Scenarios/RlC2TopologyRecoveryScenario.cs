using ResilienceLifecycle.Shared;
using Zlink.HttpClient;
using ResilienceLifecycle.Client.Support;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-C2 verifies topology state changes and request recovery after provider crash.
internal static class RlC2TopologyRecoveryScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient registry,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerB.Post("/admin/crash").SubmitRawAsync();
        await WaitUntilAsync(async () => !await IsHealthyAsync(providerB), "RL-C2 expected api-b crash.");
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitRequest("api-b", "Ready", 0))
            .SubmitAsync<TopologyEntryResult[]>();

        for (var i = 0; i < 8; i++)
        {
            var reply = (await consumer.Post("/profile/request/new-client")
                .Body(new ProfileRequest("fast", $"rl-c2-after-crash-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.ProviderRid == "api-a", "RL-C2 request used stale crashed api-b.");
        }

        await processes.StartProviderBAsync();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitRequest("api-b", "Ready", 1))
            .SubmitAsync<TopologyEntryResult[]>();
        for (var i = 0; i < 40; i++)
        {
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", $"rl-c2-restored-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-C2 restored request returned an unexpected value.");
        }

        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var waitA = providerA.Post("/evidence/wait").Body(new EvidenceWaitRequest(["marker=rl-c2-after-crash-"], [])).SubmitAsync<string[]>(timeout.Token).AsTask();
            var waitB = providerB.Post("/evidence/wait").Body(new EvidenceWaitRequest(["marker=rl-c2-after-crash-"], [])).SubmitAsync<string[]>(timeout.Token).AsTask();
            var completed = await Task.WhenAny(waitA, waitB);
            var evidence = (await completed).Body;
            timeout.Cancel();
            ScenarioAssert.That(evidence.Any(line => line.Contains("marker=rl-c2-after-crash-", StringComparison.Ordinal)), "RL-C2 did not record expected evidence 'marker=rl-c2-after-crash-'.");
        }
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var waitA = providerA.Post("/evidence/wait").Body(new EvidenceWaitRequest(["profile-request|rid=api-b|marker=rl-c2-restored-"], [])).SubmitAsync<string[]>(timeout.Token).AsTask();
            var waitB = providerB.Post("/evidence/wait").Body(new EvidenceWaitRequest(["profile-request|rid=api-b|marker=rl-c2-restored-"], [])).SubmitAsync<string[]>(timeout.Token).AsTask();
            var completed = await Task.WhenAny(waitA, waitB);
            var evidence = (await completed).Body;
            timeout.Cancel();
            ScenarioAssert.That(evidence.Any(line => line.Contains("profile-request|rid=api-b|marker=rl-c2-restored-", StringComparison.Ordinal)), "RL-C2 did not record expected evidence 'marker=rl-c2-restored-'.");
        }

        Console.WriteLine("scenario RL-C2 passed");
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
