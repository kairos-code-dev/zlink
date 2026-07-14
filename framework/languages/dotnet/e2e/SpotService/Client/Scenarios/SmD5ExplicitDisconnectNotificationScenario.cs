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
        IZlinkStreamConnector? client = null;
        try
        {
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
            Exception? last = null;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var candidate = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
                {
                    Endpoint = new Uri(sessionAStreamEndpoint),
                    ConnectTimeout = TimeSpan.FromSeconds(5),
                    RequestTimeout = TimeSpan.FromSeconds(5),
                    Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    MaxReceivedMessages = 1024
                });
                try
                {
                    await candidate.Connect.Async();
                    await candidate.Request(new UserSpotAuthReq(spotRid, actorId, "disconnect", "play-a"))
                        .PacketName("UserSpotAuthReq")
                        .Async<AuthRes>();
                    await playA.Post("/spot/create")
                        .Body(new CreateSpotReq(spotRid))
                        .Async<CreateSpotRes>();
                    await candidate.Request(new JoinUserSpotActorReq(spotRid, actorId))
                        .PacketName("JoinUserSpotActorReq")
                        .Async<JoinUserSpotActorRes>();
                    client = candidate;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await candidate.DisposeAsync();
                    await Task.Delay(200);
                }
            }

            if (client is null)
                throw new InvalidOperationException(
                    last is null
                        ? $"Actor auth did not become routable: {actorId}"
                        : $"Actor auth did not become routable: {actorId}. Last error: {last.Message}",
                    last);

            await client.Close.Async();
        }
        finally
        {
            if (client is not null) await client.DisposeAsync();
        }

        var expectedEvidence = $"spot-actor-disconnected|rid=play-a|spot={spotRid}|actor={actorId}";
        var evidence = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([expectedEvidence]))
            .Async<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(expectedEvidence, StringComparison.Ordinal)),
            "SM-D5 expected only the selected bound actor to receive disconnect notification.");

        Console.WriteLine("operation SpotService.sm-d5 passed");
    }
}
