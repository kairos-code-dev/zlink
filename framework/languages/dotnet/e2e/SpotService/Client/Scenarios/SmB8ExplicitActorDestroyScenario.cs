using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB8ExplicitActorDestroyScenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b8-destroy-{Guid.NewGuid():N}";
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
        await client.Request(new AuthReq(actorId, "destroy", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthRes>();
        var destroyed = await client.Request(new DestroyActorReq(actorId))
            .PacketName("DestroyActorReq")
            .Async<DestroyActorRes>();
        ScenarioAssert.That(destroyed.Destroyed && destroyed.ActorId == actorId, "SM-B8 destroy reply mismatch.");

        var snapshotFailed = false;
        for (var attempt = 0; attempt < 10; attempt++)
        {
            try
            {
                await client.Request(new SnapshotReq(actorId))
                    .PacketName("SnapshotReq")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<SnapshotRes>();
            }
            catch
            {
                snapshotFailed = true;
                break;
            }

            await Task.Delay(150);
        }

        ScenarioAssert.That(snapshotFailed, "SM-B8 expected request to destroyed actor to fail.");

        var evidence = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([$"actor-destroyed|rid=play-a|actor={actorId}"]))
            .Async<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line =>
                line.Contains($"actor-destroyed|rid=play-a|actor={actorId}", StringComparison.Ordinal)),
            "SM-B8 expected actor destroy evidence.");
        ScenarioAssert.That(
            evidence.All(line =>
                !line.Contains($"actor-destroy-failed|rid=play-a|actor={actorId}", StringComparison.Ordinal)),
            "SM-B8 actor destroy reported a failure.");

        Console.WriteLine("operation SpotService.sm-b8 passed");
    }
}
