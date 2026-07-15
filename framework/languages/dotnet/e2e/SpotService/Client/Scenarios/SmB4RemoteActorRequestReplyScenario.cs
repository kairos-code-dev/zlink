using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB4RemoteActorRequestReplyScenario
{
    public static async Task RunAsync(ZLinkHttpClient playB, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b4-remote-{Guid.NewGuid():N}";
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        Exception? last = null;
        ActorPingRes? reply = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            await using var client = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
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
                await client.Connect.Async();
                await client.Request(new AuthReq(actorId, "remote actor request", "play-b"))
                    .PacketName("AuthReq")
                    .Async<AuthRes>();
                reply = await client.Request(new ActorPingReq("sm-b4"))
                    .PacketName("ActorPingReq")
                    .Async<ActorPingRes>();
                break;
            }
            catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
            {
                last = ex;
                await Task.Delay(200);
            }
        }

        if (reply is null)
            throw new InvalidOperationException(
                last is null
                    ? "SM-B4 remote actor request did not become routable."
                    : $"SM-B4 remote actor request did not become routable. Last error: {last.Message}",
                last);

        ZlinkStreamAssert.Ensure(reply.NodeRid == "play-b", "SM-B4 remote actor request reached the wrong node.");
        var expectedEvidence = new[] { $"actor-ping|rid=play-b|actor={actorId}" };
        var evidence = (await playB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedEvidence))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            expectedEvidence.All(expected => evidence.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-B4 evidence did not include remote actor request.");
        Console.WriteLine("operation SpotService.sm-b4 passed");
    }
}
