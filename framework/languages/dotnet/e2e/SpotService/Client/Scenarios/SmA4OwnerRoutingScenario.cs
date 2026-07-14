using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmA4OwnerRoutingScenario
{
    public static async Task RunAsync(ZLinkHttpClient api, SpotLifecycleOrderContext context)
    {
        var reply = (await api.Post("/spot/state/request")
            .Body(new SpotStateRouteReq(context.SpotRid, "noop", 0))
            .Async<StateRes>()).Body;
        ScenarioAssert.That(reply.SpotRid == context.SpotRid, "SM-A4 request reached the wrong spot.");
        ScenarioAssert.That(reply.NodeRid == "play-a", "SM-A4 owner routing did not stay on play-a.");
        context.CurrentValue = reply.Value;
        Console.WriteLine("operation SpotService.sm-a4 passed");
    }
}
