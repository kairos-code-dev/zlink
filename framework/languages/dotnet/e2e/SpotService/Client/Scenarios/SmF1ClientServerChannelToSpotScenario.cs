using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmF1ClientServerChannelToSpotScenario
{
    public static async Task RunAsync(ZLinkHttpClient api, SpotLifecycleOrderContext context)
    {
        var state = (await api.Post("/spot/state/request")
            .Body(new SpotStateRouteReq(context.SpotRid, "add", 7))
            .Async<StateRes>()).Body;
        ZlinkStreamAssert.Ensure(state.SpotRid == context.SpotRid, "SM-F1 request reached the wrong spot.");
        ZlinkStreamAssert.Ensure(state.NodeRid == "play-a", "SM-F1 request reached the wrong node.");
        ZlinkStreamAssert.Ensure(state.Value == context.CurrentValue + 7, "SM-F1 state reply mismatch.");
        context.CurrentValue = state.Value;

        var command = (await api.Post("/spot/state/command")
            .Body(new SpotStateCommandReq(context.SpotRid, "sm-f1-command"))
            .Async<SpotStateCommandRes>()).Body;
        ZlinkStreamAssert.Ensure(command.SpotRid == context.SpotRid && command.Accepted, "SM-F1 command was not accepted.");
        var expectedEvidence = new[]
        {
            $"spot-state-request|rid=play-a|spot={context.SpotRid}|value={context.CurrentValue}",
            $"spot-state-command|rid=play-a|spot={context.SpotRid}|marker=sm-f1-command"
        };
        var evidence = (await api.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedEvidence))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            expectedEvidence.All(expected => evidence.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-F1 evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-f1 passed");
    }
}
