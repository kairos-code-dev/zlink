using Zlink.HttpClient;
using RegistryMessaging.Client;
using RegistryMessaging.Shared;

namespace RegistryMessaging.Client.Scenarios;

// RM-C4 verifies that a timed-out request does not poison later requests on
// the same discovered channel.
internal static class RmC4TimeoutIsolationScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient discoveryConsumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var timeout = (await discoveryConsumer.Post("/profile/slow-request")
            .Body(new ProfileRequest("slow"))
            .SubmitAsync<RequestFailureResult>()).Body;
        ScenarioAssert.That(timeout.Failed, "RM-C4 expected the slow request to time out.");
        ScenarioAssert.That(timeout.FailureType == nameof(TimeoutException), "RM-C4 expected TimeoutException.");

        var immediate = (await discoveryConsumer.Post("/profile/request")
            .Body(new ProfileRequest("rm-c4-after-timeout"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(immediate.Value == "profile:rm-c4-after-timeout", "RM-C4 follow-up reply mismatch.");

        await Task.Delay(TimeSpan.FromMilliseconds(1200));
        var later = (await discoveryConsumer.Post("/profile/request")
            .Body(new ProfileRequest("rm-c4-later"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(later.Value == "profile:rm-c4-later", "RM-C4 later reply mismatch.");

        var afterTimeoutEvidence = await WaitForEitherEvidenceAsync(providerA, providerB, "rm-c4-after-timeout");
        var laterEvidence = await WaitForEitherEvidenceAsync(providerA, providerB, "rm-c4-later");
        var evidence = afterTimeoutEvidence.Concat(laterEvidence).ToArray();
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("rm-c4-after-timeout", StringComparison.Ordinal))
            && evidence.Any(line => line.Contains("rm-c4-later", StringComparison.Ordinal)),
            "RM-C4 follow-up evidence missing.");
        Console.WriteLine("scenario RM-C4 passed");
    }

    static async Task<string[]> WaitForEitherEvidenceAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string contains)
    {
        var waitA = providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(contains))
            .SubmitAsync<string[]>()
            .AsTask();
        var waitB = providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(contains))
            .SubmitAsync<string[]>()
            .AsTask();
        var completed = await Task.WhenAny(waitA, waitB);
        return (await completed).Body;
    }
}
