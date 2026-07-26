// Verifies RM-C9 Backpressure behavior.
using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-C9 verifies that one-way send pressure does not expose a public send
// completion oracle and that the channel recovers for a later request.
internal static class RmC9BackpressureScenario
{
    private const int SlowSendCount = 64;
    private const int PressureEvidenceCount = 5;

    public static async Task RunAsync(ZLinkHttpClient backpressureConsumer, ZLinkHttpClient providerA)
    {
        await backpressureConsumer.Post("/profile/backpressure/reset").Async<object>();
        var marker = $"rm-c9-{Guid.NewGuid():N}";
        var outcomes = await Task.WhenAll(Enumerable.Range(0, SlowSendCount)
            .Select(index => SendBackpressureCommandAsync(
                backpressureConsumer,
                $"rm-c9-slow-{marker}-{index}")));
        ZlinkStreamAssert.Ensure(
            outcomes.All(outcome => outcome is "Submitted" or "DeadlineExceeded"),
            "RM-C9 one-way send completed outside the submit/deadline contract.");
        ZlinkStreamAssert.Ensure(
            outcomes.Contains("Submitted"),
            "RM-C9 did not admit any one-way send under pressure.");

        // Waiting for more handled messages than either configured HWM proves that the
        // provider is draining work after the burst without exposing send completion.
        var evidence = (await providerA.Post("/evidence/wait-count")
            .Body(new EvidenceCountWaitReq(marker, PressureEvidenceCount, 20000))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            evidence.Count(line => line.Contains(marker, StringComparison.Ordinal)
                                   && line.Contains("profile-command", StringComparison.Ordinal))
            >= PressureEvidenceCount,
            "RM-C9 expected provider evidence after the send burst exceeded both HWMs.");

        // The provider observes a bounded quiet period so the follow-up request is
        // issued after the slow backlog has stopped producing evidence.
        await providerA.Post("/evidence/wait-quiet")
            .Body(new EvidenceQuietWaitReq(marker))
            .Async<string[]>();

        var followUp = (await backpressureConsumer.Post("/profile/request")
            .Body(new ProfileReq("rm-c9-after"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(followUp.Value == "profile:rm-c9-after",
            "RM-C9 follow-up request failed after backlog cleared.");

        var recoveryEvidence = (await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq("rm-c9-after", 20000))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            recoveryEvidence.Any(line => line.Contains("rm-c9-after", StringComparison.Ordinal)),
            "RM-C9 recovery evidence missing.");
    }

    private static async Task<string> SendBackpressureCommandAsync(ZLinkHttpClient backpressureConsumer,
        string commandId)
    {
        return (await backpressureConsumer.Post("/profile/backpressure/send")
            .Body(new ProfileMsg(commandId))
            .Async<string>()).Body;
    }
}
