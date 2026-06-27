using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmE1Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA)
    {
        var spotRid = $"spot-sm-e1-{Guid.NewGuid():N}";
        var created = (await playA.Post("/spot/create")
            .Body(new CreateSpotReq(spotRid))
            .SubmitAsync<CreateSpotReply>()).Body;
        ScenarioAssert.That(created.SpotRid == spotRid && created.NodeRid == "play-a", "SM-E1 spot was not created on play-a.");
        var missingRequest = (await playA.Post("/spot/missing-handler/request")
            .Body(new SpotMissingHandlerReq(spotRid))
            .SubmitAsync<SpotMissingHandlerReply>()).Body;
        ScenarioAssert.That(missingRequest.Failed, "SM-E1 missing handler request did not fail.");
        var missingCommand = (await playA.Post("/spot/missing-handler/command")
            .Body(new SpotMissingCommandReq(spotRid, "missing-command"))
            .SubmitAsync<SpotMissingCommandReply>()).Body;
        ScenarioAssert.That(missingCommand.Sent, "SM-E1 missing handler command was not sent.");
        await EvidenceWait.ForAllAsync(
            playA,
            [
                "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=ReplyError|packet=MissingSpotReq",
                "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=Drop|packet=MissingSpotCommand",
            ],
            "SM-E1 missing handler evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-e1 passed");
    }
}
