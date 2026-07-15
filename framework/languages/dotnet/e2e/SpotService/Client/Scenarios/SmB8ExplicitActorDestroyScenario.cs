// Verifies SM-B8 Explicit Actor Destroy behavior.
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
        ZlinkStreamAssert.Ensure(destroyed.Destroyed && destroyed.ActorId == actorId, "SM-B8 destroy reply mismatch.");

        var evidence = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([$"actor-destroyed|rid=play-a|actor={actorId}"]))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            evidence.Any(line =>
                line.Contains($"actor-destroyed|rid=play-a|actor={actorId}", StringComparison.Ordinal)),
            "SM-B8 expected actor destroy evidence.");
        ZlinkStreamAssert.Ensure(
            evidence.All(line =>
                !line.Contains($"actor-destroy-failed|rid=play-a|actor={actorId}", StringComparison.Ordinal)),
            "SM-B8 actor destroy reported a failure.");

        await ZlinkStreamAssert.ExpectFailureAsync(
            async cancellationToken => _ = await client.Request(new SnapshotReq(actorId))
                .PacketName("SnapshotReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<SnapshotRes>(cancellationToken),
            nameof(ZlinkStreamErrorCode.RemoteError));

        Console.WriteLine("operation SpotService.sm-b8 passed");
    }
}
