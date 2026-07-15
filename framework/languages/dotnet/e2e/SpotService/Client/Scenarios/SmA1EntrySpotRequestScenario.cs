using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmA1EntrySpotRequestScenario
{
    public static async Task RunAsync(ZLinkHttpClient api, SpotLifecycleOrderContext context)
    {
        var created = (await api.Post("/spot/create")
            .Body(new CreateSpotReq(context.SpotRid))
            .Async<CreateSpotRes>()).Body;
        ZlinkStreamAssert.Ensure(created.SpotRid == context.SpotRid, "SM-A1 did not create the requested spot.");
        Console.WriteLine("operation SpotService.sm-a1 passed");
    }
}
