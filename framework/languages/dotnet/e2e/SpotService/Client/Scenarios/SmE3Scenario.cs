using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmE3Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA)
    {
        var spotRid = $"spot-sm-e3-{Guid.NewGuid():N}";
        var created = (await playA.Post("/spot/create")
            .Body(new CreateSpotReq(spotRid))
            .SubmitAsync<CreateSpotReply>()).Body;
        ScenarioAssert.That(created.SpotRid == spotRid && created.NodeRid == "play-a", "SM-E3 idle spot was not created on play-a.");
        var idle = (await playA.Post("/spot/idle-close/start")
            .Body(new SpotIdleCloseReq(spotRid, "sm-e3-idle", 50))
            .SubmitAsync<SpotIdleCloseReply>()).Body;
        ScenarioAssert.That(idle.Closed, "SM-E3 idle close did not close the spot.");
        var closedSpotRequest = (await playA.Post("/spot/missing-target/request")
            .Body(new SpotMissingTargetReq(spotRid))
            .SubmitAsync<SpotMissingTargetReply>()).Body;
        ScenarioAssert.That(closedSpotRequest.Failed, "SM-E3 closed spot request did not fail.");
        await EvidenceWait.ForAllAsync(
            playA,
            [
                $"timer-idle-close|rid=play-a|spot={spotRid}|name=sm-e3-idle|closed=True",
                $"spot-closing|rid=play-a|spot={spotRid}",
            ],
            "SM-E3 evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-e3 passed");
    }
}
