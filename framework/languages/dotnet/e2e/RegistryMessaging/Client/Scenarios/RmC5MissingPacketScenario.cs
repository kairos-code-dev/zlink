using Zlink.HttpClient;
using RegistryMessaging.Client;
using RegistryMessaging.Shared;

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
            .Body(new ProfileRequest("missing-request"))
            .SubmitAsync<RequestFailureResult>()).Body;
        ScenarioAssert.That(missingRequest.Failed, "RM-C5 missing request should fail.");

        await discoveryConsumer.Post("/profile/missing-command")
            .Body(new ProfileCommand("missing-send"))
            .SubmitAsync<object>();

        var evidence = (await WaitForDispatchErrorEvidenceAsync(
                providerA,
                providerB,
                "MissingProfileRequest"))
            .Concat(await WaitForDispatchErrorEvidenceAsync(
                providerA,
                providerB,
                "MissingProfileCommand"))
            .ToArray();
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("dispatch-error", StringComparison.Ordinal)
                && line.Contains("MissingProfileRequest", StringComparison.Ordinal)),
            "RM-C5 missing request evidence missing.");
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("dispatch-error", StringComparison.Ordinal)
                && line.Contains("MissingProfileCommand", StringComparison.Ordinal)),
            "RM-C5 missing send evidence missing.");

        var reply = (await discoveryConsumer.Post("/profile/request")
            .Body(new ProfileRequest("rm-c5-after"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:rm-c5-after", "RM-C5 normal request after negative path failed.");
        Console.WriteLine("scenario RM-C5 passed");
    }

    static async Task<string[]> WaitForDispatchErrorEvidenceAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string packetName)
    {
        var wait = new EvidenceWaitRequest(packetName);
        var providerAEvidence = providerA.Post("/evidence/wait").Body(wait).SubmitAsync<string[]>().AsTask();
        var providerBEvidence = providerB.Post("/evidence/wait").Body(wait).SubmitAsync<string[]>().AsTask();
        await Task.WhenAll(providerAEvidence, providerBEvidence);
        return providerAEvidence.Result.Body.Concat(providerBEvidence.Result.Body).ToArray();
    }
}
