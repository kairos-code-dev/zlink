// Verifies OBS-A4 Fanout And Timer behavior.
using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsA4FanoutAndTimerScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        var suffix = Guid.NewGuid().ToString("N");
        var owner = $"workflow-owner-{suffix}";
        var subscriberA = $"projection-a-{suffix}";
        var subscriberB = $"projection-b-{suffix}";
        await context.WorkflowA.Post("/workflows").Body(new CreateWorkflowReq(owner)).AsyncRaw();
        await context.WorkflowA.Post("/workflows").Body(new CreateWorkflowReq(subscriberA, "subscriber")).AsyncRaw();
        await context.WorkflowB.Post("/workflows").Body(new CreateWorkflowReq(subscriberB, "subscriber")).AsyncRaw();
        await context.WorkflowA.Post($"/workflows/{owner}/advance")
            .Body(new AdvanceWorkflowReq("obs-a4-state")).AsyncRaw();
        await context.WorkflowA.Post($"/workflows/{owner}/publish")
            .Body(new PublishProjectionReq("obs-a4-fanout")).AsyncRaw();
        var expected = $"rid={owner}|version=1|marker=obs-a4-fanout";
        var receivedA = (await context.WorkflowA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([expected], [["projection-received|"]]))
            .Async<string[]>()).Body;
        var receivedB = (await context.WorkflowB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([expected], [["projection-received|"]]))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(receivedA.Any(line => line.Contains($"subscriber={subscriberA}", StringComparison.Ordinal)),
            "OBS-A4 workflow-a subscriber did not receive the fanout.");
        ZlinkStreamAssert.Ensure(receivedB.Any(line => line.Contains($"subscriber={subscriberB}", StringComparison.Ordinal)),
            "OBS-A4 workflow-b subscriber did not receive the fanout.");

        var roomRid = $"timer-room-{suffix}";
        await context.PlayA.Post("/rooms").Body(new CreateRoomReq(roomRid)).AsyncRaw();
        var timer = await context.WaitPlayAEvidenceAsync($"timer-tick|room={roomRid}");
        ZlinkStreamAssert.Ensure(timer.Any(line => line.Contains($"timer-tick|room={roomRid}", StringComparison.Ordinal)),
            "OBS-A4 timer origin evidence missing.");
        Console.WriteLine("scenario OBS-A4 passed");
    }
}
