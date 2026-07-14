using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmA3SpecificSpotOwnerRoutingScenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, ZLinkHttpClient playB)
    {
        var spotRid = $"spot-sm-a3-{Guid.NewGuid():N}";
        var created = (await playA.Post("/spot/create")
            .Body(new CreateSpotReq(spotRid))
            .Async<CreateSpotRes>()).Body;
        ScenarioAssert.That(created.SpotRid == spotRid && created.NodeRid == "play-a",
            "SM-A3 routed spot was not created on play-a.");
        var routeReply = (await playA.Post("/spot/state/request")
            .Body(new SpotStateRouteReq(spotRid, "add", 1))
            .Async<StateRes>()).Body;
        ScenarioAssert.That(routeReply.SpotRid == spotRid, "SM-A3 route reached the wrong spot.");
        ScenarioAssert.That(routeReply.NodeRid == "play-a", "SM-A3 route reached the wrong node.");
        ScenarioAssert.That(routeReply.Value == 1, "SM-A3 state reply mismatch.");

        var expectedPlayAEvidence = new[] { $"spot-state-request|rid=play-a|spot={spotRid}|value=1" };
        var playAEvidence = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedPlayAEvidence))
            .Async<string[]>()).Body;
        ScenarioAssert.That(
            expectedPlayAEvidence.All(expected =>
                playAEvidence.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-A3 evidence mismatch.");
        var playBEvidence = (await playB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([]))
            .Async<string[]>()).Body;
        ScenarioAssert.That(
            playBEvidence.All(line => !line.Contains(
                $"spot-state-request|rid=play-b|spot={spotRid}",
                StringComparison.Ordinal)),
            "SM-A3 route resolver leaked the spot request to play-b.");
        Console.WriteLine("operation SpotService.sm-a3 passed");
    }
}
