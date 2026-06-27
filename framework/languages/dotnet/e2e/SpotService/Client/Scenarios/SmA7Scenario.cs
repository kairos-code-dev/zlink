using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmA7Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA)
    {
        var spotRid = $"spot-sm-a7-{Guid.NewGuid():N}";
        var mismatch = (await playA.Post("/spot/type-mismatch")
            .Body(new SpotTypeMismatchReq(spotRid))
            .SubmitAsync<SpotTypeMismatchReply>()).Body;
        ScenarioAssert.That(mismatch.Failed, "SM-A7 expected SpotTypeMismatch.");
        ScenarioAssert.That(mismatch.ErrorKind == "SpotTypeMismatch", "SM-A7 error kind mismatch.");
        await EvidenceWait.ForAllAsync(
            playA,
            [$"spot-type-mismatch|rid=play-a|spot={mismatch.SpotRid}|kind=SpotTypeMismatch"],
            "SM-A7 evidence did not include SpotTypeMismatch.");
        Console.WriteLine("operation SpotService.sm-a7 passed");
    }
}
