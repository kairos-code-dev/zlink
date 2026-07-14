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
        await context.WorkflowA.Post("/workflows").Body(new CreateWorkflowReq(owner)).SubmitRawAsync();
        await context.WorkflowA.Post("/workflows").Body(new CreateWorkflowReq(subscriberA, "subscriber")).SubmitRawAsync();
        await context.WorkflowB.Post("/workflows").Body(new CreateWorkflowReq(subscriberB, "subscriber")).SubmitRawAsync();
        await context.WorkflowA.Post($"/workflows/{owner}/advance")
            .Body(new AdvanceWorkflowReq("obs-a4-state")).SubmitRawAsync();
        await context.WorkflowA.Post($"/workflows/{owner}/publish")
            .Body(new PublishProjectionReq("obs-a4-fanout")).SubmitRawAsync();
        var expected = $"rid={owner}|version=1|marker=obs-a4-fanout";
        var receivedA = (await context.WorkflowA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([expected], [["projection-received|"]]))
            .SubmitAsync<string[]>()).Body;
        var receivedB = (await context.WorkflowB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([expected], [["projection-received|"]]))
            .SubmitAsync<string[]>()).Body;
        ScenarioContext.Require(receivedA.Any(line => line.Contains($"subscriber={subscriberA}", StringComparison.Ordinal)),
            "OBS-A4 workflow-a subscriber did not receive the fanout.");
        ScenarioContext.Require(receivedB.Any(line => line.Contains($"subscriber={subscriberB}", StringComparison.Ordinal)),
            "OBS-A4 workflow-b subscriber did not receive the fanout.");

        var roomRid = $"timer-room-{suffix}";
        await context.PlayA.Post("/rooms").Body(new CreateRoomReq(roomRid)).SubmitRawAsync();
        var timer = await context.WaitPlayAEvidenceAsync($"timer-tick|room={roomRid}");
        ScenarioContext.Require(timer.Any(line => line.Contains($"timer-tick|room={roomRid}", StringComparison.Ordinal)),
            "OBS-A4 timer origin evidence missing.");
        Console.WriteLine("scenario OBS-A4 passed");
    }
}
