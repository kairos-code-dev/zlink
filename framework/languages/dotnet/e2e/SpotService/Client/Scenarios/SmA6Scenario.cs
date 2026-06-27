using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmA6Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA)
    {
        var spotRid = $"spot-sm-a6-{Guid.NewGuid():N}";
        var created = (await playA.Post("/spot/create")
            .Body(new CreateSpotReq(spotRid))
            .SubmitAsync<CreateSpotReply>()).Body;
        ScenarioAssert.That(created.SpotRid == spotRid && created.NodeRid == "play-a", "SM-A6 lifecycle spot was not created on play-a.");
        var closeReply = (await playA.Post("/spot/close")
            .Body(new CloseSpotReq(spotRid))
            .SubmitAsync<CloseSpotReply>()).Body;
        ScenarioAssert.That(closeReply.Closed, "SM-A6 did not close the lifecycle spot.");
        await EvidenceWait.ForAllAsync(
            playA,
            [
                $"spot-initialize|rid=play-a|spot={spotRid}",
                $"spot-closing|rid=play-a|spot={spotRid}",
            ],
            "SM-A6 evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-a6 passed");
    }
}
