// Verifies RM-A6 Multiple Channels behavior.
using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-A6 verifies that profile and workflow channels registered in the same
// location store (same key prefix) keep isolated peer row sets per mesh name
// and do not cross-route messages.
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
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(profileReply.Value == $"profile:{profileMarker}", "RM-A6 profile reply value mismatch.");
        ZlinkStreamAssert.Ensure(
            profileReply.ProviderRid is "api-a" or "api-b",
            "RM-A6 profile request reached an unexpected provider.");

        var workflowReply = (await workflow.Post("/workflow/request")
            .Body(new WorkflowReq(workflowMarker))
            .Async<WorkflowRes>()).Body;
        ZlinkStreamAssert.Ensure(workflowReply.Value == $"workflow:{workflowMarker}",
            "RM-A6 workflow reply value mismatch.");
        ZlinkStreamAssert.Ensure(workflowReply.ProviderRid == "workflow-a",
            "RM-A6 workflow request should reach workflow-a.");

        // Mesh-name filtered runtime query rows must not mix across channels
        // even though they share one store and key prefix (doc RM-A6).
        var profileRows = (await providerA.Get("/locations/peers?mesh=profile").Async<PeerLocationRow[]>()).Body;
        var workflowRows = (await providerA.Get("/locations/peers?mesh=workflow").Async<PeerLocationRow[]>()).Body;
        ZlinkStreamAssert.Ensure(
            profileRows.Length > 0 && profileRows.All(row => row.MeshName == "profile"),
            "RM-A6 profile mesh filter returned rows from another mesh.");
        ZlinkStreamAssert.Ensure(
            workflowRows.Length > 0 && workflowRows.All(row => row.MeshName == "workflow"),
            "RM-A6 workflow mesh filter returned rows from another mesh.");
        ZlinkStreamAssert.Ensure(
            workflowRows.Any(row => row.Role == "Router" && row.NodeRid == "workflow-a")
            && workflowRows.All(row => row.NodeRid is not ("api-a" or "api-b")),
            "RM-A6 workflow mesh rows should contain workflow-a only.");

        var providerWaitA = providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(profileMarker))
            .Async<string[]>()
            .AsTask();
        var providerWaitB = providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(profileMarker))
            .Async<string[]>()
            .AsTask();
        var providerCompleted = await Task.WhenAny(providerWaitA, providerWaitB);
        var providerEvidence = (await providerCompleted).Body;
        var workflowEvidence = (await workflow.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(workflowMarker))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            providerEvidence.Any(line => line.Contains("profile-request|", StringComparison.Ordinal)
                                         && line.Contains(profileMarker, StringComparison.Ordinal)),
            "RM-A6 profile evidence missing.");
        ZlinkStreamAssert.Ensure(
            workflowEvidence.Any(line => line.Contains("workflow-request|rid=workflow-a", StringComparison.Ordinal)
                                         && line.Contains(workflowMarker, StringComparison.Ordinal)),
            "RM-A6 workflow evidence missing.");
        ZlinkStreamAssert.Ensure(
            !providerEvidence.Any(line => line.Contains("workflow-request|", StringComparison.Ordinal)
                                          && line.Contains(workflowMarker, StringComparison.Ordinal)),
            "RM-A6 workflow request was recorded on profile providers.");
    }
}
