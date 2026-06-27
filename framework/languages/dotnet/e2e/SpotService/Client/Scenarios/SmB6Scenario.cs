using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;
using SpotService.Client.Support;

namespace SpotService.Client.Scenarios;

internal static class SmB6Scenario
{
    public static async Task RunAsync(
        ZLinkHttpClient playA,
        ZLinkHttpClient sessionA,
        string sessionAStreamEndpoint)
    {
        var spotRid = $"spot-sm-b6-{Guid.NewGuid():N}";
        var leaveActorId = $"actor-sm-b6-left-{Guid.NewGuid():N}";
        var disconnectActorId = $"actor-sm-b6-disconnected-{Guid.NewGuid():N}";

        LeaveReply? left = null;
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var leaveStarted = false;
            try
            {
                await using var client = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
                {
                    Endpoint = new Uri(sessionAStreamEndpoint),
                    ConnectTimeout = TimeSpan.FromSeconds(5),
                    RequestTimeout = TimeSpan.FromSeconds(5),
                    Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    MaxReceivedMessages = 1024,
                });
                await client.Connect.Async();
                await client.Request(new UserSpotAuthReq(spotRid, leaveActorId, leaveActorId, "play-a"))
                    .PacketName("UserSpotAuthReq")
                    .Async<AuthReply>();

                leaveStarted = true;
                left = await client.Request(new LeaveReq(leaveActorId))
                    .PacketName("LeaveReq")
                    .Async<LeaveReply>();
                break;
            }
            catch (Exception ex) when (!leaveStarted && ex is ZlinkStreamException or TimeoutException)
            {
                last = ex;
                await Task.Delay(500);
            }
        }

        if (left is null)
        {
            throw new InvalidOperationException(
                last is null ? "SM-B6 leave flow did not become routable." : $"SM-B6 leave flow did not become routable. Last error: {last.Message}",
                last);
        }

        ScenarioAssert.That(left.Accepted && left.ActorId == leaveActorId, "SM-B6 leave reply mismatch.");
        var expectedLeaveEvidence = new[] { $"spot-actor-left|rid=play-a|spot={spotRid}|actor={leaveActorId}" };
        var playAAfterLeave = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(expectedLeaveEvidence))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            expectedLeaveEvidence.All(expected => playAAfterLeave.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-B6 expected explicit leave evidence.");
        ScenarioAssert.That(
            playAAfterLeave.Any(line => line.Contains(
                $"spot-actor-left|rid=play-a|spot={spotRid}|actor={leaveActorId}",
                StringComparison.Ordinal)),
            "SM-B6 expected explicit leave evidence.");
        ScenarioAssert.That(
            playAAfterLeave.All(line => !line.Contains(
                $"spot-actor-disconnected|rid=play-a|spot={spotRid}|actor={leaveActorId}",
                StringComparison.Ordinal)),
            "SM-B6 explicit leave incorrectly emitted disconnect evidence.");

        await using (var disconnectClient = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(sessionAStreamEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024,
        }))
        {
            await disconnectClient.Connect.Async();
            await disconnectClient.Request(new AuthReq(disconnectActorId, "disconnect", "session-a"))
                .PacketName("AuthReq")
                .Async<AuthReply>();
            await disconnectClient.Close.Async();
        }

        var sessionAfterDisconnect = (await sessionA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest([$"entry-disconnected|rid=session-a|actor={disconnectActorId}"]))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            sessionAfterDisconnect.Any(line => line.Contains(
                $"entry-disconnected|rid=session-a|actor={disconnectActorId}",
                StringComparison.Ordinal)),
            "SM-B6 expected disconnect evidence.");
        ScenarioAssert.That(
            sessionAfterDisconnect.All(line => !line.Contains(
                $"entry-left|rid=session-a|actor={disconnectActorId}",
                StringComparison.Ordinal)),
            "SM-B6 disconnect incorrectly emitted leave evidence.");

        Console.WriteLine("operation SpotService.sm-b6 passed");
    }
}
