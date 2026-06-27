using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

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

        var left = await RunLeaveFlowWithRetryAsync(sessionAStreamEndpoint, spotRid, leaveActorId);
        ScenarioAssert.That(left.Accepted && left.ActorId == leaveActorId, "SM-B6 leave reply mismatch.");
        var playAAfterLeave = await EvidenceWait.ForAllAsync(
            playA,
            [$"spot-actor-left|rid=play-a|spot={spotRid}|actor={leaveActorId}"],
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

        await using (var disconnectClient = CreateClient(sessionAStreamEndpoint))
        {
            await disconnectClient.Connect.Async();
            await disconnectClient.Request(new AuthReq(disconnectActorId, "disconnect", "session-a"))
                .PacketName("AuthReq")
                .Async<AuthReply>();
            await disconnectClient.Close.Async();
        }

        var sessionAfterDisconnect = await EvidenceWait.ForAsync(
            sessionA,
            new EvidenceWaitRequest([$"entry-disconnected|rid=session-a|actor={disconnectActorId}"]),
            evidence => evidence.Any(line => line.Contains(
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

    static async Task<LeaveReply> RunLeaveFlowWithRetryAsync(string endpoint, string spotRid, string actorId)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var leaveStarted = false;
            try
            {
                await using var client = CreateClient(endpoint);
                await client.Connect.Async();
                await client.Request(new UserSpotAuthReq(spotRid, actorId, actorId, "play-a"))
                    .PacketName("UserSpotAuthReq")
                    .Async<AuthReply>();

                leaveStarted = true;
                return await client.Request(new LeaveReq(actorId))
                    .PacketName("LeaveReq")
                    .Async<LeaveReply>();
            }
            catch (Exception ex) when (!leaveStarted && ex is ZlinkStreamException or TimeoutException)
            {
                last = ex;
                await Task.Delay(500);
            }
        }

        throw new InvalidOperationException(
            last is null ? "SM-B6 leave flow did not become routable." : $"SM-B6 leave flow did not become routable. Last error: {last.Message}",
            last);
    }

}
