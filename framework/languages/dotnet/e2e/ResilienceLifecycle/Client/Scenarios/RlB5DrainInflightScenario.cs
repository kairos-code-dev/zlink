using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-B5 verifies in-flight work can finish while drain is active.
internal static class RlB5DrainInflightScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerA.Post("/admin/restore").SubmitRawAsync();
        await providerB.Post("/admin/restore").SubmitRawAsync();
        await WaitForWeightAsync(providerA, 100);
        await WaitForWeightAsync(providerB, 100);

        var slowMarker = $"rl-b5-slow-{Guid.NewGuid():N}";
        var slowTask = consumer.Post("/profile/request")
            .Body(new ProfileRequest("slow", slowMarker))
            .SubmitAsync<ProfileReply>();

        var slowProvider = await WaitForSlowStartAsync(providerA, providerB, slowMarker);
        var drainedProvider = slowProvider == "api-a" ? providerA : providerB;
        var healthyProvider = slowProvider == "api-a" ? "api-b" : "api-a";
        var beforeDrain = (await drainedProvider.Get("/evidence").SubmitAsync<string[]>()).Body;

        await drainedProvider.Post("/admin/drain").SubmitRawAsync();
        await WaitForWeightAsync(drainedProvider, 0);

        for (var i = 0; i < 12; i++)
        {
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", $"rl-b5-drained-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.ProviderRid == healthyProvider, "RL-B5 drain did not block new requests to the drained provider.");
        }

        var slowReply = (await slowTask).Body;
        ScenarioAssert.That(
            slowReply.ProviderRid == slowProvider && slowReply.Marker == slowMarker,
            "RL-B5 in-flight slow reply did not complete on the drained provider.");

        var afterDrain = (await drainedProvider.Get("/evidence").SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            CountNew(afterDrain, beforeDrain, $"profile-request|rid={slowProvider}|marker=rl-b5-drained-") == 0,
            "RL-B5 drained provider accepted new requests after drain.");
        ScenarioAssert.That(
            afterDrain.Any(line => line.Contains($"profile-request|rid={slowProvider}|marker={slowMarker}", StringComparison.Ordinal)),
            "RL-B5 in-flight completion evidence missing.");

        await drainedProvider.Post("/admin/restore").SubmitRawAsync();
        await WaitForWeightAsync(drainedProvider, 100);

        for (var i = 0; i < 40; i++)
        {
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", $"rl-b5-after-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-B5 restored request returned an unexpected value.");
        }

        await drainedProvider.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest([$"profile-request|rid={slowProvider}|marker=rl-b5-after-"], []))
            .SubmitAsync<string[]>();

        Console.WriteLine("scenario RL-B5 passed");
    }

    static async Task<string> WaitForSlowStartAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string marker)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        var waitA = providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest([$"profile-start|rid=api-a|marker={marker}"], []))
            .SubmitAsync<string[]>(timeout.Token)
            .AsTask();
        var waitB = providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest([$"profile-start|rid=api-b|marker={marker}"], []))
            .SubmitAsync<string[]>(timeout.Token)
            .AsTask();

        var completed = await Task.WhenAny(waitA, waitB);
        await completed;
        timeout.Cancel();
        return completed == waitA ? "api-a" : "api-b";
    }

    static async Task WaitForWeightAsync(ZLinkHttpClient provider, int expected)
    {
        await provider.Post("/admin/weight/wait")
            .Body(new WeightWaitRequest(expected))
            .SubmitRawAsync();
    }

    static int CountNew(string[] after, string[] before, string pattern) =>
        Math.Max(0, after.Count(line => line.Contains(pattern, StringComparison.Ordinal))
            - before.Count(line => line.Contains(pattern, StringComparison.Ordinal)));
}
