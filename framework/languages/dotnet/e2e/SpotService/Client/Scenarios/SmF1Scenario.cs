using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmF1Scenario
{
    public static async Task RunAsync(ZLinkHttpClient api, SpotLifecycleOrderContext context)
    {
        var state = (await api.Post("/spot/state/request")
            .Body(new SpotStateRouteReq(context.SpotRid, "add", 7))
            .SubmitAsync<StateReply>()).Body;
        ScenarioAssert.That(state.SpotRid == context.SpotRid, "SM-F1 request reached the wrong spot.");
        ScenarioAssert.That(state.NodeRid == "play-a", "SM-F1 request reached the wrong node.");
        ScenarioAssert.That(state.Value == context.CurrentValue + 7, "SM-F1 state reply mismatch.");
        context.CurrentValue = state.Value;

        var command = (await api.Post("/spot/state/command")
            .Body(new SpotStateCommandReq(context.SpotRid, "sm-f1-command"))
            .SubmitAsync<SpotStateCommandReply>()).Body;
        ScenarioAssert.That(command.SpotRid == context.SpotRid && command.Accepted, "SM-F1 command was not accepted.");
        await EvidenceWait.ForAllAsync(
            api,
            [
                $"spot-state-request|rid=play-a|spot={context.SpotRid}|value={context.CurrentValue}",
                $"spot-state-command|rid=play-a|spot={context.SpotRid}|marker=sm-f1-command",
            ],
            "SM-F1 evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-f1 passed");
    }
}
