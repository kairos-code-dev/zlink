// Verifies the OBS-C3 fixed MeshNode drain behavior.
using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsC3FixedDrainScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        var suffix = Guid.NewGuid().ToString("N");
        var roomRid = $"room-c3-{suffix}";
        var room = (await context.PlayA.Post("/rooms")
            .Body(new CreateRoomReq(roomRid, "fixed-drain"))
            .Async<CreateRoomRes>()).Body;
        ZlinkStreamAssert.Ensure(room.RoomRid == roomRid && room.NodeRid == "play-a",
            "OBS-C3 initial local Spot creation failed.");
        var roomBeforeDrain = (await context.PlayB.Get("/evidence")
            .Query("spotRid", roomRid).Async<EvidenceSnapshot>()).Body;
        ZlinkStreamAssert.Ensure(roomBeforeDrain.SpotRows.Any(row => row.SpotRid == roomRid),
            "OBS-C3 normal request did not leave the Spot registered.");

        await context.PlayA.Post("/operation-gate/arm")
            .Query("maximumWaitMs", "10000").AsyncRaw();
        var acceptedTurn = context.PlayA.Post("/operation/start")
            .Body(new PlayBoundedOperationReq($"accepted-{suffix}"))
            .Async<PlayBoundedOperationRes>().AsTask();
        await context.PlayA.Post("/operation-gate/wait-started")
            .Query("timeoutMs", "5000").AsyncRaw();
        await context.PlayA.Post("/drain?deadlineMs=10000").AsyncRaw();
        await context.PlayA.Post("/drain?deadlineMs=10000").AsyncRaw();

        var draining = roomBeforeDrain;
        for (var attempt = 0; attempt < 100; attempt++)
        {
            draining = (await context.PlayB.Get("/evidence")
                .Query("spotRid", roomRid).Async<EvidenceSnapshot>()).Body;
            if (draining.PeerRows.Any(row => row.NodeRid == "play-a" && row.Draining)) break;
            await Task.Delay(50);
        }
        ZlinkStreamAssert.Ensure(
            draining.PeerRows.Any(row => row.NodeRid == "play-a" && row.Draining),
            "OBS-C3 Play draining marker was not published.");
        ZlinkStreamAssert.Ensure(draining.SpotRows.Any(row => row.SpotRid == roomRid),
            "OBS-C3 accepted turn did not keep local Spots available until completion.");

        var rejectedAdmission = await context.PlayA.Post("/rooms")
            .Body(new CreateRoomReq($"rejected-{suffix}", "fixed-drain"))
            .AsyncRaw();
        ZlinkStreamAssert.Ensure(rejectedAdmission.Status >= 400,
            "OBS-C3 accepted a new Spot after application admission was sealed.");

        await context.PlayA.Post("/operation-gate/release").AsyncRaw();
        var completedTurn = (await acceptedTurn).Body;
        ZlinkStreamAssert.Ensure(
            completedTurn.Marker == $"accepted-{suffix}" && completedTurn.NodeRid == "play-a",
            "OBS-C3 drain did not complete the bounded turn accepted before sealing.");
        var playDrain = await ScenarioContext.WaitForDrainAsync(
            context.PlayA, TimeSpan.FromSeconds(12));
        ZlinkStreamAssert.Ensure(playDrain.Result == "Drained",
            $"OBS-C3 fixed Play drain did not finish cleanly: {playDrain.Result}/{playDrain.Reason}.");
        var repeatedPlayDrain = await ScenarioContext.WaitForDrainAsync(
            context.PlayA, TimeSpan.FromSeconds(12));
        ZlinkStreamAssert.Ensure(
            playDrain.TerminalCount == 1 && repeatedPlayDrain.TerminalCount == 1,
            "OBS-C3 Play drain published more than one terminal result.");
        var playEvidence = (await context.PlayA.Get("/evidence")
            .Query("spotRid", roomRid).Async<EvidenceSnapshot>()).Body;
        ZlinkStreamAssert.Ensure(playEvidence.SpotRows.All(row => row.SpotRid != roomRid),
            "OBS-C3 local Spot row was not removed after the Spot closed.");

        var workflowRid = $"workflow-c3-{suffix}";
        await context.WorkflowA.Post("/workflows")
            .Body(new CreateWorkflowReq(workflowRid)).AsyncRaw();
        var initialRow = (await context.WorkflowB.Get("/evidence")
            .Query("spotRid", workflowRid).Async<EvidenceSnapshot>()).Body
            .SpotRows.Single(row => row.SpotRid == workflowRid);
        ZlinkStreamAssert.Ensure(initialRow.NodeRid == "workflow-a" && initialRow.Generation > 0,
            "OBS-C3 initial Workflow Spot generation was not published.");

        var remoteRead = (await context.WorkflowB.Get($"/workflows/{workflowRid}/state")
            .Async<ReadWorkflowRes>()).Body;
        ZlinkStreamAssert.Ensure(remoteRead.NodeRid == "workflow-a" && remoteRead.Version == 0,
            "OBS-C3 existing remote resolve/request path regressed.");
        var remoteSend = await context.WorkflowB.Post($"/workflows/{workflowRid}/signal")
            .Body(new WorkflowSignalReq($"remote-send-{suffix}"))
            .AsyncRaw();
        ZlinkStreamAssert.Ensure(remoteSend.Status == 200,
            "OBS-C3 existing remote resolve/send path rejected the message.");
        await context.WorkflowA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                [$"workflow-signal|rid={workflowRid}|marker=remote-send-{suffix}|node=workflow-a"],
                [], 5000))
            .Async<string[]>();

        var advanced = (await context.WorkflowB.Post($"/workflows/{workflowRid}/advance")
            .Body(new AdvanceWorkflowReq("obs-c3-persisted"))
            .Async<AdvanceWorkflowRes>()).Body;
        ZlinkStreamAssert.Ensure(advanced.NodeRid == "workflow-a" && advanced.Version == 1,
            "OBS-C3 remote Workflow request did not reach the existing Spot.");

        await context.WorkflowB.Post($"/workflows/{workflowRid}/stale-handle/capture")
            .AsyncRaw();
        await context.WorkflowA.Post("/drain?deadlineMs=10000").AsyncRaw();
        await context.WorkflowA.Post("/drain?deadlineMs=10000").AsyncRaw();
        var workflowDrain = await ScenarioContext.WaitForDrainAsync(
            context.WorkflowA, TimeSpan.FromSeconds(12));
        ZlinkStreamAssert.Ensure(workflowDrain.Result == "Drained",
            $"OBS-C3 fixed Workflow drain did not finish cleanly: {workflowDrain.Result}/{workflowDrain.Reason}.");
        var repeatedWorkflowDrain = await ScenarioContext.WaitForDrainAsync(
            context.WorkflowA, TimeSpan.FromSeconds(12));
        ZlinkStreamAssert.Ensure(
            workflowDrain.TerminalCount == 1 && repeatedWorkflowDrain.TerminalCount == 1,
            "OBS-C3 Workflow drain published more than one terminal result.");
        var released = (await context.WorkflowB.Get("/evidence")
            .Query("spotRid", workflowRid).Async<EvidenceSnapshot>()).Body;
        ZlinkStreamAssert.Ensure(released.SpotRows.All(row => row.SpotRid != workflowRid),
            "OBS-C3 fixed drain left the Workflow Spot row registered.");

        var stale = (await context.WorkflowB.Post("/stale-handle/execute")
            .Async<StaleHandleProbeRes>()).Body;
        ZlinkStreamAssert.Ensure(stale.Failed,
            "OBS-C3 stale remote handle unexpectedly reached or recreated the drained Spot.");
        var afterStale = (await context.WorkflowB.Get("/evidence")
            .Query("spotRid", workflowRid).Async<EvidenceSnapshot>()).Body;
        ZlinkStreamAssert.Ensure(
            afterStale.SpotRows.All(row => row.SpotRid != workflowRid)
            && afterStale.Entries.All(entry =>
                !entry.Contains($"workflow-created|rid={workflowRid}", StringComparison.Ordinal)),
            "OBS-C3 stale handle caused a hidden remote GetOrCreate/materialization.");

        await context.WorkflowB.Post("/workflows")
            .Body(new CreateWorkflowReq(workflowRid)).AsyncRaw();
        var recreated = (await context.WorkflowB.Get("/evidence")
            .Query("spotRid", workflowRid).Async<EvidenceSnapshot>()).Body
            .SpotRows.Single(row => row.SpotRid == workflowRid);
        ZlinkStreamAssert.Ensure(
            recreated.NodeRid == "workflow-b"
            && recreated.Generation > 0
            && (recreated.NodeRid != initialRow.NodeRid
                || recreated.Generation != initialRow.Generation),
            "OBS-C3 explicit local GetOrCreate did not publish a new Spot generation.");
        var replayed = (await context.WorkflowB.Get($"/workflows/{workflowRid}/state")
            .Async<ReadWorkflowRes>()).Body;
        ZlinkStreamAssert.Ensure(replayed.NodeRid == "workflow-b"
                                && replayed.Version == 1
                                && replayed.State == "obs-c3-persisted",
            "OBS-C3 workflow state was not replayed on workflow-b.");
        Console.WriteLine("scenario OBS-C3 passed");
    }
}
