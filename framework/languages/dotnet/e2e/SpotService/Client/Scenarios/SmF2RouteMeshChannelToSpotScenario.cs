// Verifies SM-F2 Route Mesh Channel To Spot behavior.
using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmF2RouteMeshChannelToSpotScenario
{
    public static async Task RunAsync(ZLinkHttpClient api)
    {
        var spotRid = $"spot-sm-f2-{Guid.NewGuid():N}";
        await api.Post("/spot/create").Body(new CreateSpotReq(spotRid)).Async<CreateSpotRes>();
        var state = (await api.Post("/spot/state/request")
            .Body(new SpotStateRouteReq(spotRid, "add", 5))
            .Async<StateRes>()).Body;
        ZlinkStreamAssert.Ensure(state.SpotRid == spotRid, "SM-F2 request reached the wrong spot.");
        ZlinkStreamAssert.Ensure(state.NodeRid == "play-a", "SM-F2 request reached the wrong node.");
        ZlinkStreamAssert.Ensure(state.Value == 5, "SM-F2 state reply mismatch.");

        var command = (await api.Post("/spot/state/command")
            .Body(new SpotStateCommandReq(spotRid, "sm-f2-command"))
            .Async<SpotStateCommandRes>()).Body;
        ZlinkStreamAssert.Ensure(command.SpotRid == spotRid && command.Accepted, "SM-F2 command was not accepted.");
        var expectedEvidence = new[]
        {
            $"spot-state-request|rid=play-a|spot={spotRid}|value=5",
            $"spot-state-command|rid=play-a|spot={spotRid}|marker=sm-f2-command"
        };
        var evidence = (await api.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedEvidence))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            expectedEvidence.All(expected => evidence.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-F2 evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-f2 passed");
    }
}
