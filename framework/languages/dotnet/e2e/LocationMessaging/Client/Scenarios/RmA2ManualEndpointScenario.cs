using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-A2 verifies that a role server endpoint can request a profile provider through an
// explicitly configured channel endpoint without the location store.
internal static class RmA2ManualEndpointScenario
{
    public static async Task RunAsync(ZLinkHttpClient providerA)
    {
        var reply = (await providerA.Post("/profile/manual").Body(new ProfileReq("rm-a2"))
            .Async<ProfileRes>()).Body;
        ScenarioAssert.That(reply.Value == "profile:rm-a2", "RM-A2 reply value mismatch.");
        ScenarioAssert.That(reply.ProviderRid == "api-a", "RM-A2 manual endpoint should reach api-a.");

        var evidence = (await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq("value=rm-a2"))
            .Async<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("value=rm-a2", StringComparison.Ordinal)),
            "RM-A2 api-a evidence missing.");
    }
}