// Verifies SM-B1 Local Actor Join behavior.
using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB1LocalActorJoinScenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b1-local-{Guid.NewGuid():N}";
        ZlinkStreamAssert.Ensure(!string.IsNullOrWhiteSpace(sessionAStreamEndpoint),
            "session-a stream endpoint is required.");
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
        await client.Request(new AuthReq(actorId, "local actor", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthRes>();
        var ping = await client.Request(new ActorPingReq("b1"))
            .PacketName("ActorPingReq")
            .Async<ActorPingRes>();
        ZlinkStreamAssert.Ensure(ping.ActorId == actorId, "SM-B1 actor reply mismatch.");
        ZlinkStreamAssert.Ensure(ping.NodeRid == "play-a", "SM-B1 local node mismatch.");
        var expectedEvidence = new[]
        {
            $"entry-created|rid=play-a|actor={actorId}",
            $"entry-joined|rid=play-a|actor={actorId}"
        };
        var evidence = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedEvidence))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            expectedEvidence.All(expected => evidence.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-B1 evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-b1 passed");
    }
}
