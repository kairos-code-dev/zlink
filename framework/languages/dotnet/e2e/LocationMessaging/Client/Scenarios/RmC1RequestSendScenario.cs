// Verifies RM-C1 Request Send behavior.
using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-C1 verifies the normal request/reply and one-way send paths for a
// location-store auto-connected profile channel.
internal static class RmC1RequestSendScenario
{
    public static async Task RunAsync(ZLinkHttpClient providerA, ZLinkHttpClient providerB)
    {
        var reply = (await providerA.Post("/profile/request")
            .Body(new ProfileReq("rm-c1-request"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(reply.Value == "profile:rm-c1-request", "RM-C1 request reply mismatch.");

        var commandId = $"cmd-{Guid.NewGuid():N}";
        await providerA.Post("/profile/command")
            .Body(new ProfileMsg(commandId))
            .Async<object>();

        var evidence = await WaitForProviderEvidenceAsync(providerA, providerB, commandId);
        ZlinkStreamAssert.Ensure(
            evidence.Any(line => line.Contains("profile-request|", StringComparison.Ordinal)
                                 && line.Contains("rm-c1-request", StringComparison.Ordinal)),
            "RM-C1 request evidence missing.");
        ZlinkStreamAssert.Ensure(
            evidence.Any(line => line.Contains("profile-command|", StringComparison.Ordinal)),
            "RM-C1 send evidence missing.");
    }

    private static async Task<string[]> WaitForProviderEvidenceAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string commandId)
    {
        // ChannelName send is select-one (spec 11 §2): the command lands on
        // exactly one member, so the wait completes on whichever provider
        // received it and the other side contributes its current snapshot.
        var wait = new EvidenceWaitReq($"command={commandId}");
        var providerAEvidence = providerA.Post("/evidence/wait").Body(wait).Async<string[]>().AsTask();
        var providerBEvidence = providerB.Post("/evidence/wait").Body(wait).Async<string[]>().AsTask();
        var winner = await Task.WhenAny(providerAEvidence, providerBEvidence);
        var other = ReferenceEquals(winner, providerAEvidence) ? providerB : providerA;
        var otherSnapshot = (await other.Get("/evidence").Async<string[]>()).Body;
        return (await winner).Body.Concat(otherSnapshot).ToArray();
    }
}
