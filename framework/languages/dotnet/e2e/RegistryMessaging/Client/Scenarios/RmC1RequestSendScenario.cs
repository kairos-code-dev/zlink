using RegistryMessaging.Client.Support;
using RegistryMessaging.Shared;
using Zlink.HttpClient;

namespace RegistryMessaging.Client.Scenarios;

// RM-C1 verifies the normal request/reply and one-way send paths for a
// discovered profile channel.
internal static class RmC1RequestSendScenario
{
    public static async Task RunAsync(ZLinkHttpClient providerA, ZLinkHttpClient providerB)
    {
        var reply = (await providerA.Post("/profile/request")
            .Body(new ProfileRequest("rm-c1-request"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:rm-c1-request", "RM-C1 request reply mismatch.");

        var commandId = $"cmd-{Guid.NewGuid():N}";
        await providerA.Post("/profile/command")
            .Body(new ProfileCommand(commandId))
            .SubmitAsync<object>();

        var evidence = await WaitForProviderEvidenceAsync(providerA, providerB, commandId);
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("profile-request|", StringComparison.Ordinal)
                                 && line.Contains("rm-c1-request", StringComparison.Ordinal)),
            "RM-C1 request evidence missing.");
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("profile-command|", StringComparison.Ordinal)),
            "RM-C1 send evidence missing.");
        Console.WriteLine("scenario RM-C1 passed");
    }

    private static async Task<string[]> WaitForProviderEvidenceAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string commandId)
    {
        var wait = new EvidenceWaitRequest($"command={commandId}");
        var providerAEvidence = providerA.Post("/evidence/wait").Body(wait).SubmitAsync<string[]>().AsTask();
        var providerBEvidence = providerB.Post("/evidence/wait").Body(wait).SubmitAsync<string[]>().AsTask();
        await Task.WhenAll(providerAEvidence, providerBEvidence);
        return providerAEvidence.Result.Body.Concat(providerBEvidence.Result.Body).ToArray();
    }
}