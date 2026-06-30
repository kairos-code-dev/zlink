using RegistryMessaging.Client.Support;
using RegistryMessaging.Shared;
using Zlink.HttpClient;

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

        var afterTimeoutWaitA = providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest("rm-c4-after-timeout"))
            .SubmitAsync<string[]>()
            .AsTask();
        var afterTimeoutWaitB = providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest("rm-c4-after-timeout"))
            .SubmitAsync<string[]>()
            .AsTask();
        var afterTimeoutCompleted = await Task.WhenAny(afterTimeoutWaitA, afterTimeoutWaitB);
        var afterTimeoutEvidence = (await afterTimeoutCompleted).Body;

        var laterWaitA = providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest("rm-c4-later"))
            .SubmitAsync<string[]>()
            .AsTask();
        var laterWaitB = providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest("rm-c4-later"))
            .SubmitAsync<string[]>()
            .AsTask();
        var laterCompleted = await Task.WhenAny(laterWaitA, laterWaitB);
        var laterEvidence = (await laterCompleted).Body;
        var evidence = afterTimeoutEvidence.Concat(laterEvidence).ToArray();
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("rm-c4-after-timeout", StringComparison.Ordinal))
            && evidence.Any(line => line.Contains("rm-c4-later", StringComparison.Ordinal)),
            "RM-C4 follow-up evidence missing.");
        Console.WriteLine("scenario RM-C4 passed");
    }
}