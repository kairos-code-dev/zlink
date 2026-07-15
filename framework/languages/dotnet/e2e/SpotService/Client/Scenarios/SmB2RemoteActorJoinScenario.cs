// Verifies SM-B2 Remote Actor Join behavior.
using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB2RemoteActorJoinScenario
{
    public static async Task RunAsync(ZLinkHttpClient playB, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b2-remote-{Guid.NewGuid():N}";
        await using var client = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(sessionAStreamEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024
        });
        await client.Connect.Async();
        await client.Request(new AuthReq(actorId, "remote actor", "play-b"))
            .PacketName("AuthReq")
            .Async<AuthRes>();
        var reply = await client.Request(new ActorPingReq("b2"))
            .PacketName("ActorPingReq")
            .Async<ActorPingRes>();

        ZlinkStreamAssert.Ensure(reply.ActorId == actorId, "SM-B2 actor reply mismatch.");
        ZlinkStreamAssert.Ensure(reply.NodeRid == "play-b", "SM-B2 remote node mismatch.");
        var expectedEvidence = new[]
        {
            $"entry-created|rid=play-b|actor={actorId}",
            $"entry-joined|rid=play-b|actor={actorId}"
        };
        var evidence = (await playB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedEvidence))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            expectedEvidence.All(expected => evidence.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-B2 remote lifecycle evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-b2 passed");
    }
}
