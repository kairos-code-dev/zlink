using SpotService.Client;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB4Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playB, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b4-remote-{Guid.NewGuid():N}";
        var replies = await SpotActorRequestSupport.RunBoundActorRequestsWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq(actorId, "remote actor request", "play-b"),
            [new ActorPingReq("sm-b4")],
            "SM-B4 remote actor request did not become routable.");
        var reply = replies.Single();
        ScenarioAssert.That(reply.NodeRid == "play-b", "SM-B4 remote actor request reached the wrong node.");
        await EvidenceWait.ForAllAsync(
            playB,
            [$"actor-ping|rid=play-b|actor={actorId}"],
            "SM-B4 evidence did not include remote actor request.");
        Console.WriteLine("operation SpotService.sm-b4 passed");
    }
}
