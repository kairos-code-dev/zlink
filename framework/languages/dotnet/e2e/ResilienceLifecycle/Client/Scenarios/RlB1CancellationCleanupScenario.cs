using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-B1 verifies timeout/cancellation cleanup and a successful follow-up request.
internal static class RlB1CancellationCleanupScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var slowMarker = $"rl-b1-slow-{Guid.NewGuid():N}";
        var timeout = await consumer.Post("/profile/request/timeout/100")
            .Body(new ProfileRequest("slow", slowMarker))
            .SubmitRawAsync();
        ScenarioAssert.That(timeout.Status == 408, "RL-B1 expected the slow request to time out.");

        var followUpMarker = $"rl-b1-follow-up-{Guid.NewGuid():N}";
        var followUp = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("fast", followUpMarker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(followUp.Value == "profile:fast", "RL-B1 follow-up request failed after timeout.");

        await WaitForEitherEvidenceAsync(providerA, providerB, slowMarker);

        var later = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("fast", $"rl-b1-later-{Guid.NewGuid():N}"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(later.Value == "profile:fast", "RL-B1 later request failed after slow completion.");

        Console.WriteLine("scenario RL-B1 passed");
    }

    static async Task WaitForEitherEvidenceAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string marker)
    {
        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            new EvidenceWaitRequest(["profile-request|", $"marker={marker}"], []),
            line => line.Contains($"marker={marker}", StringComparison.Ordinal),
            "RL-B1 slow request completion evidence missing.");
    }
}
