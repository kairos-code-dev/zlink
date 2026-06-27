using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB8Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b8-destroy-{Guid.NewGuid():N}";
        await using var client = CreateClient(sessionAStreamEndpoint);
        await client.Connect.Async();
        await client.Request(new AuthReq(actorId, "destroy", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthReply>();
        var destroyed = await client.Request(new DestroyActorReq(actorId))
            .PacketName("DestroyActorReq")
            .Async<DestroyActorReply>();
        ScenarioAssert.That(destroyed.Destroyed && destroyed.ActorId == actorId, "SM-B8 destroy reply mismatch.");

        await WaitForSnapshotFailureAsync(client, actorId);

        var evidence = await EvidenceWait.ForAsync(
            playA,
            new EvidenceWaitRequest([$"actor-destroyed|rid=play-a|actor={actorId}"]),
            evidence => evidence.Any(line => line.Contains($"actor-destroyed|rid=play-a|actor={actorId}", StringComparison.Ordinal)),
            "SM-B8 expected actor destroy evidence.");
        ScenarioAssert.That(
            evidence.All(line => !line.Contains($"actor-destroy-failed|rid=play-a|actor={actorId}", StringComparison.Ordinal)),
            "SM-B8 actor destroy reported a failure.");

        Console.WriteLine("operation SpotService.sm-b8 passed");
    }

    static IZlinkStreamConnector CreateClient(string endpoint)
    {
        ScenarioAssert.That(!string.IsNullOrWhiteSpace(endpoint), "session-a stream endpoint is required.");
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024,
        });
    }

    static async Task WaitForSnapshotFailureAsync(IZlinkStreamConnector client, string actorId)
    {
        for (var attempt = 0; attempt < 10; attempt++)
        {
            try
            {
                await client.Request(new SnapshotReq(actorId))
                    .PacketName("SnapshotReq")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<SnapshotReply>();
            }
            catch
            {
                return;
            }

            await Task.Delay(150);
        }

        throw new InvalidOperationException("SM-B8 expected request to destroyed actor to fail.");
    }

}
