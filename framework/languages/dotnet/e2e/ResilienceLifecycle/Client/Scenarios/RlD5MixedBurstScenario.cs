using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-D5 verifies mixed request/send burst traffic.
internal static class RlD5MixedBurstScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var requestTasks = Enumerable.Range(0, 60).Select(async i =>
        {
            return (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", $"rl-d5-req-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
        });
        var sendTasks = Enumerable.Range(0, 60).Select(i =>
            consumer.Post("/profile/command")
                .Body(new ProfileCommand($"rl-d5-cmd-{i}"))
                .SubmitRawAsync().AsTask());
        var replies = await Task.WhenAll(requestTasks);
        await Task.WhenAll(sendTasks);
        ScenarioAssert.That(
            replies.All(reply => reply.Value == "profile:fast"),
            "RL-D5 mixed workload replies were invalid.");

        var evidence = await WaitForMixedEvidenceAsync(providerA, providerB);
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("marker=rl-d5-req-", StringComparison.Ordinal)),
            "RL-D5 did not record expected evidence 'marker=rl-d5-req-'.");
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("marker=rl-d5-cmd-", StringComparison.Ordinal)),
            "RL-D5 did not record expected evidence 'marker=rl-d5-cmd-'.");

        Console.WriteLine("scenario RL-D5 passed");
    }

    static async Task<string[]> WaitForMixedEvidenceAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var requestEvidence = await WaitForEitherProviderAsync(providerA, providerB, "marker=rl-d5-req-");
        var commandEvidence = await WaitForEitherProviderAsync(providerA, providerB, "marker=rl-d5-cmd-");
        return requestEvidence.Concat(commandEvidence).ToArray();
    }

    static async Task<string[]> WaitForEitherProviderAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string markerPrefix)
    {
        var evidence = await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            markerPrefix,
            $"RL-D5 evidence missing for {markerPrefix}.");
        return evidence;
    }
}
