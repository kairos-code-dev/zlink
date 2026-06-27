using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

// Verifies app-driven owner remap across play-a and play-b role servers.
internal static class SmG2Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, ZLinkHttpClient playB)
    {
        var key = $"key-sm-g2-{Guid.NewGuid():N}";
        var firstOwnerSpotRid = $"spot-{key}-a";
        var secondOwnerSpotRid = $"spot-{key}-b";
        var firstCreated = (await playA.Post("/spot/create")
            .Body(new CreateSpotReq(firstOwnerSpotRid))
            .SubmitAsync<CreateSpotReply>()).Body;
        var firstReply = (await playA.Post("/spot/state/request")
            .Body(new SpotStateRouteReq(firstOwnerSpotRid, "add", 1))
            .SubmitAsync<StateReply>()).Body;
        var firstEvidence = await EvidenceWait.ForAllAsync(
            playA,
            [$"spot-state-request|rid=play-a|spot={firstOwnerSpotRid}|value=1"],
            "SM-G2 play-a evidence did not include the first owner request.");

        var secondCreated = (await playB.Post("/spot/create")
            .Body(new CreateSpotReq(secondOwnerSpotRid))
            .SubmitAsync<CreateSpotReply>()).Body;
        var secondReply = (await playB.Post("/spot/state/request")
            .Body(new SpotStateRouteReq(secondOwnerSpotRid, "add", 1))
            .SubmitAsync<StateReply>()).Body;
        var secondEvidence = await EvidenceWait.ForAllAsync(
            playB,
            [$"spot-state-request|rid=play-b|spot={secondOwnerSpotRid}|value=1"],
            "SM-G2 play-b evidence did not include the remapped owner request.");

        ScenarioAssert.That(firstCreated.NodeRid == "play-a", "SM-G2 first owner was not created on play-a.");
        ScenarioAssert.That(secondCreated.NodeRid == "play-b", "SM-G2 remapped owner was not created on play-b.");
        ScenarioAssert.That(firstReply.NodeRid == "play-a", "SM-G2 first owner request reached the wrong node.");
        ScenarioAssert.That(secondReply.NodeRid == "play-b", "SM-G2 remapped owner request reached the wrong node.");
        ScenarioAssert.That(
            !firstEvidence.Any(line => line.Contains($"spot-state-request|rid=play-a|spot={secondOwnerSpotRid}", StringComparison.Ordinal)),
            "SM-G2 remapped owner leaked to play-a.");
        ScenarioAssert.That(
            !secondEvidence.Any(line => line.Contains($"spot-state-request|rid=play-b|spot={firstOwnerSpotRid}", StringComparison.Ordinal)),
            "SM-G2 first owner leaked to play-b.");

        Console.WriteLine("operation SpotService.sm-g2 passed");
    }
}
