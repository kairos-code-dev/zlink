using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB5Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b5-missing-{Guid.NewGuid():N}";
        await using var client = SpotActorRequestSupport.CreateClient(sessionAStreamEndpoint);
        await client.Connect.Async();
        await client.Request(new AuthReq(actorId, "missing handler actor", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthReply>();
        await SpotActorRequestSupport.ExpectFailureAsync(
            client.Request(new ActorPingReq("missing-handler"))
                .PacketName("MissingActorReq")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<ActorPingReply>().AsTask(),
            "SM-B5 expected missing actor handler request to fail.");
        await EvidenceWait.ForAllAsync(
            playA,
            ["dispatch-error|surface=SpotActor|reason=HandlerMissing|action=ReplyError|packet=MissingActorReq"],
            "SM-B5 evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-b5 passed");
    }
}
