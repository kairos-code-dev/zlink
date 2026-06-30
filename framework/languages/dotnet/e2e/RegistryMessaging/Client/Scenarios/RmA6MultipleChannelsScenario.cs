using RegistryMessaging.Client.Support;
using RegistryMessaging.Shared;
using Zlink.HttpClient;

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
            .Body(new ProfileReq(profileMarker))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(profileReply.Value == $"profile:{profileMarker}", "RM-A6 profile reply value mismatch.");
        ScenarioAssert.That(
            profileReply.ProviderRid is "api-a" or "api-b",
            "RM-A6 profile request reached an unexpected provider.");

        var workflowReply = (await workflow.Post("/workflow/request")
            .Body(new WorkflowReq(workflowMarker))
            .SubmitAsync<WorkflowRes>()).Body;
        ScenarioAssert.That(workflowReply.Value == $"workflow:{workflowMarker}",
            "RM-A6 workflow reply value mismatch.");
        ScenarioAssert.That(workflowReply.ProviderRid == "workflow-a",
            "RM-A6 workflow request should reach workflow-a.");

        var providerWaitA = providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(profileMarker))
            .SubmitAsync<string[]>()
            .AsTask();
        var providerWaitB = providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(profileMarker))
            .SubmitAsync<string[]>()
            .AsTask();
        var providerCompleted = await Task.WhenAny(providerWaitA, providerWaitB);
        var providerEvidence = (await providerCompleted).Body;
        var workflowEvidence = (await workflow.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(workflowMarker))
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
}