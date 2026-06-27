using Zlink.HttpClient;
using RegistryMessaging.Client;
using RegistryMessaging.Shared;

namespace RegistryMessaging.Client.Scenarios;

// RM-A6 verifies that profile and workflow channels advertised through the same
// registry are resolved independently and do not cross-route messages.
internal static class RmA6MultipleChannelsScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        ZLinkHttpClient workflow)
    {
        var profileMarker = $"rm-a6-profile-{Guid.NewGuid():N}";
        var workflowMarker = $"rm-a6-workflow-{Guid.NewGuid():N}";

        var profileReply = (await providerA.Post("/profile/request")
            .Body(new ProfileRequest(profileMarker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(profileReply.Value == $"profile:{profileMarker}", "RM-A6 profile reply value mismatch.");
        ScenarioAssert.That(
            profileReply.ProviderRid is "api-a" or "api-b",
            "RM-A6 profile request reached an unexpected provider.");

        var workflowReply = (await workflow.Post("/workflow/request")
            .Body(new WorkflowRequest(workflowMarker))
            .SubmitAsync<WorkflowReply>()).Body;
        ScenarioAssert.That(workflowReply.Value == $"workflow:{workflowMarker}", "RM-A6 workflow reply value mismatch.");
        ScenarioAssert.That(workflowReply.ProviderRid == "workflow-a", "RM-A6 workflow request should reach workflow-a.");

        var providerEvidence = await WaitForEitherEvidenceAsync(providerA, providerB, profileMarker);
        var workflowEvidence = (await workflow.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(workflowMarker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            providerEvidence.Any(line => line.Contains("profile-request|", StringComparison.Ordinal)
                && line.Contains(profileMarker, StringComparison.Ordinal)),
            "RM-A6 profile evidence missing.");
        ScenarioAssert.That(
            workflowEvidence.Any(line => line.Contains("workflow-request|rid=workflow-a", StringComparison.Ordinal)
                && line.Contains(workflowMarker, StringComparison.Ordinal)),
            "RM-A6 workflow evidence missing.");
        ScenarioAssert.That(
            !providerEvidence.Any(line => line.Contains("workflow-request|", StringComparison.Ordinal)
                && line.Contains(workflowMarker, StringComparison.Ordinal)),
            "RM-A6 workflow request was recorded on profile providers.");
        Console.WriteLine("scenario RM-A6 passed");
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
