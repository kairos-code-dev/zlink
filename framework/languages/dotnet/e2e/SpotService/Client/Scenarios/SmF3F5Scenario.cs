using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmF3F5Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, ZLinkHttpClient gateway)
    {
        var spotRid = $"sm-f3-f5-{Guid.NewGuid():N}";
        var created = (await playA.Post("/spot/create")
            .Body(new CreateSpotReq(spotRid))
            .SubmitAsync<CreateSpotRes>()).Body;
        ScenarioAssert.That(created.SpotRid == spotRid, "SM-F3 target spot was not created.");

        var before = await RoutePingAsync(gateway, "sm-f3-before");
        ScenarioAssert.That(
            before is { Value: "sm-f3-before", NodeRid: "play-a" },
            "SM-F3 ordinary route request did not use the shared route channel.");

        var state = (await gateway.Post("/spot/route-state")
            .Body(new SpotStateRouteReq(spotRid, "add", 3))
            .SubmitAsync<StateRes>()).Body;
        ScenarioAssert.That(
            state is { SpotRid: var actualSpotRid, NodeRid: "play-a", Value: 3 }
            && actualSpotRid == spotRid,
            "SM-F3 spot route request was not dispatched to its target spot.");

        var afterMixed = await RoutePingAsync(gateway, "sm-f3-after");
        ScenarioAssert.That(
            afterMixed is { Value: "sm-f3-after", NodeRid: "play-a" },
            "SM-F3 spot routing interfered with ordinary route messaging.");
        Console.WriteLine("SM-F3 PASS");

        var closed = (await playA.Post("/spot/close")
            .Body(new CloseSpotReq(spotRid))
            .SubmitAsync<CloseSpotRes>()).Body;
        ScenarioAssert.That(closed.Closed, "SM-F5 target spot did not close.");

        var closedSpotRouteFailed = false;
        try
        {
            await gateway.Post("/spot/route-state")
                .Body(new SpotStateRouteReq(spotRid, "add", 1))
                .SubmitAsync<StateRes>();
        }
        catch
        {
            closedSpotRouteFailed = true;
        }
        ScenarioAssert.That(
            closedSpotRouteFailed,
            "SM-F5 closed target spot still accepted a routed request.");

        var afterClose = await RoutePingAsync(gateway, "sm-f5-after-close");
        ScenarioAssert.That(
            afterClose is { Value: "sm-f5-after-close", NodeRid: "play-a" },
            "SM-F5 closing the routed spot stopped ordinary route messaging.");

        var expectedEvidence = new[]
        {
            "control-ping|rid=play-a|value=sm-f3-before",
            $"spot-state-request|rid=play-a|spot={spotRid}|value=3",
            "control-ping|rid=play-a|value=sm-f3-after",
            $"spot-closing|rid=play-a|spot={spotRid}",
            "control-ping|rid=play-a|value=sm-f5-after-close"
        };
        var evidence = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedEvidence))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            expectedEvidence.All(expected => evidence.Any(line =>
                line.Contains(expected, StringComparison.Ordinal))),
            "SM-F3/SM-F5 evidence mismatch.");
        Console.WriteLine("SM-F5 PASS");
    }

    private static async Task<ControlPingRes> RoutePingAsync(
        ZLinkHttpClient gateway,
        string marker)
    {
        return (await gateway.Post("/channel/route-ping")
            .Body(new ControlPingReq(marker))
            .SubmitAsync<ControlPingRes>()).Body;
    }
}
