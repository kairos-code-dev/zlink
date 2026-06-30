using ResilienceLifecycle.Client.Support;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-C1 verifies repeated client lifecycle traffic and a follow-up request.
internal static class RlC1ClientHostLifecycleScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        foreach (var index in Enumerable.Range(0, 12))
        {
            var reply = (await consumer.Post("/profile/request/new-client")
                .Body(new ProfileRequest("fast", $"rl-c1-{index}"))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(
                reply.Value == "profile:fast",
                "RL-C1 request failed before cleanup.");
        }

        var followUp = (await consumer.Post("/profile/request/new-client")
            .Body(new ProfileRequest("fast", "rl-c1-after-cleanup"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(followUp.Value == "profile:fast", "RL-C1 follow-up failed after client cleanup.");

        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var waitA = providerA.Post("/evidence/wait").Body(new EvidenceWaitRequest(["marker=rl-c1-"], []))
                .SubmitAsync<string[]>(timeout.Token).AsTask();
            var waitB = providerB.Post("/evidence/wait").Body(new EvidenceWaitRequest(["marker=rl-c1-"], []))
                .SubmitAsync<string[]>(timeout.Token).AsTask();
            var completed = await Task.WhenAny(waitA, waitB);
            var evidence = (await completed).Body;
            timeout.Cancel();
            ScenarioAssert.That(evidence.Any(line => line.Contains("marker=rl-c1-", StringComparison.Ordinal)),
                "RL-C1 did not record expected evidence 'marker=rl-c1-'.");
        }
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var waitA = providerA.Post("/evidence/wait")
                .Body(new EvidenceWaitRequest(["marker=rl-c1-after-cleanup"], [])).SubmitAsync<string[]>(timeout.Token)
                .AsTask();
            var waitB = providerB.Post("/evidence/wait")
                .Body(new EvidenceWaitRequest(["marker=rl-c1-after-cleanup"], [])).SubmitAsync<string[]>(timeout.Token)
                .AsTask();
            var completed = await Task.WhenAny(waitA, waitB);
            var evidence = (await completed).Body;
            timeout.Cancel();
            ScenarioAssert.That(
                evidence.Any(line => line.Contains("marker=rl-c1-after-cleanup", StringComparison.Ordinal)),
                "RL-C1 did not record expected evidence 'marker=rl-c1-after-cleanup'.");
        }

        Console.WriteLine("scenario RL-C1 passed");
    }
}