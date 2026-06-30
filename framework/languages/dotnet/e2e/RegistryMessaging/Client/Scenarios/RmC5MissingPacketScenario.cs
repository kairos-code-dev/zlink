using RegistryMessaging.Client.Support;
using RegistryMessaging.Shared;
using Zlink.HttpClient;

namespace RegistryMessaging.Client.Scenarios;

// RM-C5 verifies that missing packet registrations are reported as dispatch
// errors and that normal traffic still works afterward.
internal static class RmC5MissingPacketScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient discoveryConsumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var missingRequest = (await discoveryConsumer.Post("/profile/missing-request")
            .Body(new ProfileReq("missing-request"))
            .SubmitAsync<RequestFailureRes>()).Body;
        ScenarioAssert.That(missingRequest.Failed, "RM-C5 missing request should fail.");

        await discoveryConsumer.Post("/profile/missing-command")
            .Body(new ProfileMsg("missing-send"))
            .SubmitAsync<object>();

        var evidence = (await WaitForDispatchErrorEvidenceAsync(
                providerA,
                providerB,
                "MissingProfileReq"))
            .Concat(await WaitForDispatchErrorEvidenceAsync(
                providerA,
                providerB,
                "MissingProfileMsg"))
            .ToArray();
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("dispatch-error", StringComparison.Ordinal)
                                 && line.Contains("MissingProfileReq", StringComparison.Ordinal)),
            "RM-C5 missing request evidence missing.");
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("dispatch-error", StringComparison.Ordinal)
                                 && line.Contains("MissingProfileMsg", StringComparison.Ordinal)),
            "RM-C5 missing send evidence missing.");

        var reply = (await discoveryConsumer.Post("/profile/request")
            .Body(new ProfileReq("rm-c5-after"))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(reply.Value == "profile:rm-c5-after", "RM-C5 normal request after negative path failed.");
        Console.WriteLine("scenario RM-C5 passed");
    }

    private static async Task<string[]> WaitForDispatchErrorEvidenceAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string packetName)
    {
        var wait = new EvidenceWaitReq(packetName);
        var providerAEvidence = providerA.Post("/evidence/wait").Body(wait).SubmitAsync<string[]>().AsTask();
        var providerBEvidence = providerB.Post("/evidence/wait").Body(wait).SubmitAsync<string[]>().AsTask();
        await Task.WhenAll(providerAEvidence, providerBEvidence);
        return providerAEvidence.Result.Body.Concat(providerBEvidence.Result.Body).ToArray();
    }
}