using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-C4 verifies that a timed-out request does not poison later requests on
// the same location-store auto-connected channel.
internal static class RmC4TimeoutIsolationScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient storeConsumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var timeout = (await storeConsumer.Post("/profile/slow-request")
            .Body(new ProfileReq("slow"))
            .Async<RequestFailureRes>()).Body;
        ZlinkStreamAssert.Ensure(timeout.Failed, "RM-C4 expected the slow request to time out.");
        ZlinkStreamAssert.Ensure(timeout.FailureType == nameof(TimeoutException), "RM-C4 expected TimeoutException.");

        var immediate = (await storeConsumer.Post("/profile/request")
            .Body(new ProfileReq("rm-c4-after-timeout"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(immediate.Value == "profile:rm-c4-after-timeout", "RM-C4 follow-up reply mismatch.");

        await Task.Delay(TimeSpan.FromMilliseconds(1200));
        var later = (await storeConsumer.Post("/profile/request")
            .Body(new ProfileReq("rm-c4-later"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(later.Value == "profile:rm-c4-later", "RM-C4 later reply mismatch.");

        var afterTimeoutWaitA = providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq("rm-c4-after-timeout"))
            .Async<string[]>()
            .AsTask();
        var afterTimeoutWaitB = providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq("rm-c4-after-timeout"))
            .Async<string[]>()
            .AsTask();
        var afterTimeoutCompleted = await Task.WhenAny(afterTimeoutWaitA, afterTimeoutWaitB);
        var afterTimeoutEvidence = (await afterTimeoutCompleted).Body;

        var laterWaitA = providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq("rm-c4-later"))
            .Async<string[]>()
            .AsTask();
        var laterWaitB = providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq("rm-c4-later"))
            .Async<string[]>()
            .AsTask();
        var laterCompleted = await Task.WhenAny(laterWaitA, laterWaitB);
        var laterEvidence = (await laterCompleted).Body;
        var evidence = afterTimeoutEvidence.Concat(laterEvidence).ToArray();
        ZlinkStreamAssert.Ensure(
            evidence.Any(line => line.Contains("rm-c4-after-timeout", StringComparison.Ordinal))
            && evidence.Any(line => line.Contains("rm-c4-later", StringComparison.Ordinal)),
            "RM-C4 follow-up evidence missing.");
    }
}