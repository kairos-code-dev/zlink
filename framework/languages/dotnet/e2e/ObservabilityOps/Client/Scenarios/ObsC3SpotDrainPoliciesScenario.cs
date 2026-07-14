using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsC3SpotDrainPoliciesScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        var suffix = Guid.NewGuid().ToString("N");
        var roomRid = $"room-c3-{suffix}";
        await context.PlayA.Post("/rooms")
            .Body(new CreateRoomReq(roomRid, "auto-close")).AsyncRaw();
        await context.PlayA.Post("/drain?deadlineMs=10000").AsyncRaw();
        var duringNatural = (await context.PlayA.Get("/evidence").Async<EvidenceSnapshot>()).Body;
        ScenarioContext.Require(duringNatural.SpotRows.Any(row => row.SpotRid == roomRid),
            "OBS-C3 drain-natural removed the room before its application lifetime ended.");
        var playDrain = await ScenarioContext.WaitForDrainAsync(
            context.PlayA, TimeSpan.FromSeconds(12));
        ScenarioContext.Require(playDrain.Result == "Drained",
            $"OBS-C3 drain-natural did not finish cleanly: {playDrain.Result}/{playDrain.Reason}.");
        var playMetrics = (await context.PlayA.Get("/evidence").Async<EvidenceSnapshot>()).Body.Metrics;
        ScenarioContext.Require(playMetrics.Any(sample => sample.Name == "zlink.drain.rooms.drained"
                                                          && sample.Value >= 1
                                                          && sample.Tags.GetValueOrDefault("policy") == "drain_natural"),
            "OBS-C3 drain-natural room metric was not recorded.");

        var workflowRid = $"workflow-c3-{suffix}";
        await context.WorkflowA.Post("/workflows")
            .Body(new CreateWorkflowReq(workflowRid)).AsyncRaw();
        var advanced = (await context.WorkflowA.Post($"/workflows/{workflowRid}/advance")
            .Body(new AdvanceWorkflowReq("obs-c3-persisted"))
            .Async<AdvanceWorkflowRes>()).Body;
        ScenarioContext.Require(advanced.Version == 1, "OBS-C3 initial workflow version mismatch.");
        await context.WorkflowA.Post("/drain?deadlineMs=10000").AsyncRaw();
        var workflowDrain = await ScenarioContext.WaitForDrainAsync(
            context.WorkflowA, TimeSpan.FromSeconds(12));
        ScenarioContext.Require(workflowDrain.Result == "Drained",
            $"OBS-C3 release-and-recreate did not drain cleanly: {workflowDrain.Result}/{workflowDrain.Reason}.");
        var released = (await context.WorkflowB.Get("/evidence").Async<EvidenceSnapshot>()).Body;
        ScenarioContext.Require(released.SpotRows.All(row => row.SpotRid != workflowRid),
            "OBS-C3 release-and-recreate left the owner Spot row registered.");
        await context.WorkflowB.Post("/workflows")
            .Body(new CreateWorkflowReq(workflowRid)).AsyncRaw();
        var replayed = (await context.WorkflowB.Get($"/workflows/{workflowRid}/state")
            .Async<ReadWorkflowRes>()).Body;
        ScenarioContext.Require(replayed.NodeRid == "workflow-b"
                                && replayed.Version == 1
                                && replayed.State == "obs-c3-persisted",
            "OBS-C3 workflow state was not replayed on workflow-b.");
        var workflowMetrics = (await context.WorkflowA.Get("/evidence")
            .Async<EvidenceSnapshot>()).Body.Metrics;
        ScenarioContext.Require(workflowMetrics.Any(sample => sample.Name == "zlink.drain.rooms.drained"
                                                              && sample.Value >= 1
                                                              && sample.Tags.GetValueOrDefault("policy") == "release_and_recreate"),
            "OBS-C3 release-and-recreate metric was not recorded.");
        Console.WriteLine("scenario OBS-C3 passed");
    }

}
