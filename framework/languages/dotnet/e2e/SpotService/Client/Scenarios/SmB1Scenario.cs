using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB1Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b1-local-{Guid.NewGuid():N}";
        await using var client = SpotActorRequestSupport.CreateClient(sessionAStreamEndpoint);
        await client.Connect.Async();
        await client.Request(new AuthReq(actorId, "local actor", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthReply>();
        var ping = await client.Request(new ActorPingReq("b1"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(ping.ActorId == actorId, "SM-B1 actor reply mismatch.");
        ScenarioAssert.That(ping.NodeRid == "play-a", "SM-B1 local node mismatch.");
        await EvidenceWait.ForAllAsync(
            playA,
            [
                $"entry-created|rid=play-a|actor={actorId}",
                $"entry-joined|rid=play-a|actor={actorId}",
            ],
            "SM-B1 evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-b1 passed");
    }
}
