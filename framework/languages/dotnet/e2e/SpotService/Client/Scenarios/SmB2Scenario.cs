using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB2Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playB, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b2-remote-{Guid.NewGuid():N}";
        var reply = (await SpotActorRequestSupport.RunBoundActorRequestsWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq(actorId, "remote actor", "play-b"),
            [new ActorPingReq("b2")],
            "SM-B2 remote actor flow did not become routable.")).Single();
        ScenarioAssert.That(reply.ActorId == actorId, "SM-B2 actor reply mismatch.");
        ScenarioAssert.That(reply.NodeRid == "play-b", "SM-B2 remote node mismatch.");
        await EvidenceWait.ForAllAsync(
            playB,
            [
                $"entry-created|rid=play-b|actor={actorId}",
                $"entry-joined|rid=play-b|actor={actorId}",
            ],
            "SM-B2 remote lifecycle evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-b2 passed");
    }
}
