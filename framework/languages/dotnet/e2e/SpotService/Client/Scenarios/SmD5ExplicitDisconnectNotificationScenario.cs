// Verifies SM-D5 Explicit Disconnect Notification behavior.
using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmD5ExplicitDisconnectNotificationScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient playA,
        ZLinkHttpClient sessionA,
        string sessionAStreamEndpoint)
    {
        _ = sessionA;
        var spotRid = $"spot-sm-d5-{Guid.NewGuid():N}";
        var actorId = $"actor-sm-d5-notified-{Guid.NewGuid():N}";
        await using (var client = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(sessionAStreamEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024
        }))
        {
            await client.Connect.Async();
        await client.Request(new UserSpotAuthReq(spotRid, actorId, "disconnect"))
                .PacketName("UserSpotAuthReq").Async<AuthRes>();
            await playA.Post("/spot/create").Body(new CreateSpotReq(spotRid)).Async<CreateSpotRes>();
            await client.Request(new JoinUserSpotActorReq(spotRid, actorId))
                .PacketName("JoinUserSpotActorReq").Async<JoinUserSpotActorRes>();
            await client.Close.Async();
        }

        var expectedEvidence = $"spot-actor-disconnected|rid=play-a|spot={spotRid}|actor={actorId}";
        var evidence = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([expectedEvidence]))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            evidence.Any(line => line.Contains(expectedEvidence, StringComparison.Ordinal)),
            "SM-D5 expected only the selected bound actor to receive disconnect notification.");

        Console.WriteLine("operation SpotService.sm-d5 passed");
    }
}
