using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmF5ClosedSpotRouteIsolationScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient playA,
        ZLinkHttpClient gateway,
        string spotRid)
    {
        var closed = (await playA.Post("/spot/close")
            .Body(new CloseSpotReq(spotRid))
            .Async<CloseSpotRes>()).Body;
        ScenarioAssert.That(closed.Closed, "SM-F5 target spot did not close.");

        var closedSpotRouteFailed = false;
        try
        {
            await gateway.Post("/spot/route-state")
                .Body(new SpotStateRouteReq(spotRid, "add", 1))
                .Async<StateRes>();
        }
        catch
        {
            closedSpotRouteFailed = true;
        }
        ScenarioAssert.That(
            closedSpotRouteFailed,
            "SM-F5 closed target spot still accepted a routed request.");

        var afterClose = (await gateway.Post("/channel/route-ping")
            .Body(new ControlPingReq("sm-f5-after-close"))
            .Async<ControlPingRes>()).Body;
        ScenarioAssert.That(
            afterClose is { Value: "sm-f5-after-close", NodeRid: "play-a" },
            "SM-F5 closing the routed spot stopped ordinary route messaging.");

        var expectedEvidence = new[]
        {
            $"spot-closing|rid=play-a|spot={spotRid}",
            "control-ping|rid=play-a|value=sm-f5-after-close"
        };
        var evidence = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedEvidence))
            .Async<string[]>()).Body;
        ScenarioAssert.That(
            expectedEvidence.All(expected => evidence.Any(line =>
                line.Contains(expected, StringComparison.Ordinal))),
            "SM-F5 evidence mismatch.");
        Console.WriteLine("SM-F5 PASS");
    }
}
