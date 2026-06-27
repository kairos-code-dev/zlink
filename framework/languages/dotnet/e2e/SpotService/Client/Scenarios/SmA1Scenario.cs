using SpotService.Shared;
using Zlink.HttpClient;
using SpotService.Client.Support;

namespace SpotService.Client.Scenarios;

internal static class SmA1Scenario
{
    public static async Task RunAsync(ZLinkHttpClient api, SpotLifecycleOrderContext context)
    {
        var created = (await api.Post("/spot/create")
            .Body(new CreateSpotReq(context.SpotRid))
            .SubmitAsync<CreateSpotReply>()).Body;
        ScenarioAssert.That(created.SpotRid == context.SpotRid, "SM-A1 did not create the requested spot.");
        Console.WriteLine("operation SpotService.sm-a1 passed");
    }
}
