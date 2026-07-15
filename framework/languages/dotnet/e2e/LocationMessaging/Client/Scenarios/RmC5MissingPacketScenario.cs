// Verifies RM-C5 Missing Packet behavior.
using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-C5 verifies that missing packet registrations are reported as dispatch
// errors and that normal traffic still works afterward.
internal static class RmC5MissingPacketScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient storeConsumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var missingRequest = (await storeConsumer.Post("/profile/missing-request")
            .Body(new ProfileReq("missing-request"))
            .Async<RequestFailureRes>()).Body;
        ZlinkStreamAssert.Ensure(missingRequest.Failed, "RM-C5 missing request should fail.");

        await storeConsumer.Post("/profile/missing-command")
            .Body(new ProfileMsg("missing-send"))
            .Async<object>();

        var evidence = (await WaitForDispatchErrorEvidenceAsync(
                providerA,
                providerB,
                "MissingProfileReq"))
            .Concat(await WaitForDispatchErrorEvidenceAsync(
                providerA,
                providerB,
                "MissingProfileMsg"))
            .ToArray();
        ZlinkStreamAssert.Ensure(
            evidence.Any(line => line.Contains("dispatch-error", StringComparison.Ordinal)
                                 && line.Contains("MissingProfileReq", StringComparison.Ordinal)),
            "RM-C5 missing request evidence missing.");
        ZlinkStreamAssert.Ensure(
            evidence.Any(line => line.Contains("dispatch-error", StringComparison.Ordinal)
                                 && line.Contains("MissingProfileMsg", StringComparison.Ordinal)),
            "RM-C5 missing send evidence missing.");

        var reply = (await storeConsumer.Post("/profile/request")
            .Body(new ProfileReq("rm-c5-after"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(reply.Value == "profile:rm-c5-after", "RM-C5 normal request after negative path failed.");
    }

    private static async Task<string[]> WaitForDispatchErrorEvidenceAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string packetName)
    {
        var wait = new EvidenceWaitReq(packetName);
        var providerAEvidence = providerA.Post("/evidence/wait").Body(wait).Async<string[]>().AsTask();
        var providerBEvidence = providerB.Post("/evidence/wait").Body(wait).Async<string[]>().AsTask();
        await Task.WhenAll(providerAEvidence, providerBEvidence);
        return (await providerAEvidence).Body.Concat((await providerBEvidence).Body).ToArray();
    }
}
