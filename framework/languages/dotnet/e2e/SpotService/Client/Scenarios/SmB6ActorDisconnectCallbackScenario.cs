// Verifies SM-B6 Actor Disconnect Callback behavior.
using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB6ActorDisconnectCallbackScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient playA,
        ZLinkHttpClient sessionA,
        string sessionAStreamEndpoint)
    {
        var spotRid = $"spot-sm-b6-{Guid.NewGuid():N}";
        var leaveActorId = $"actor-sm-b6-left-{Guid.NewGuid():N}";
        var disconnectActorId = $"actor-sm-b6-disconnected-{Guid.NewGuid():N}";

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
        await client.Request(new UserSpotAuthReq(spotRid, leaveActorId, leaveActorId))
                .PacketName("UserSpotAuthReq")
                .Async<AuthRes>();
            await playA.Post("/spot/create")
                .Body(new CreateSpotReq(spotRid))
                .Async<CreateSpotRes>();
            await client.Request(new JoinUserSpotActorReq(spotRid, leaveActorId))
                .PacketName("JoinUserSpotActorReq")
                .Async<JoinUserSpotActorRes>();
            var left = await client.Request(new LeaveReq(leaveActorId))
                .PacketName("LeaveReq")
                .Async<LeaveRes>();

            ZlinkStreamAssert.Ensure(left.Accepted && left.ActorId == leaveActorId, "SM-B6 leave reply mismatch.");
        }
        var expectedLeaveEvidence = new[] { $"spot-actor-left|rid=play-a|spot={spotRid}|actor={leaveActorId}" };
        var playAAfterLeave = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(expectedLeaveEvidence))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            expectedLeaveEvidence.All(expected =>
                playAAfterLeave.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            "SM-B6 expected explicit leave evidence.");
        ZlinkStreamAssert.Ensure(
            playAAfterLeave.Any(line => line.Contains(
                $"spot-actor-left|rid=play-a|spot={spotRid}|actor={leaveActorId}",
                StringComparison.Ordinal)),
            "SM-B6 expected explicit leave evidence.");
        ZlinkStreamAssert.Ensure(
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
            MaxReceivedMessages = 1024
        }))
        {
            await disconnectClient.Connect.Async();
        await disconnectClient.Request(new UserSpotAuthReq(spotRid, disconnectActorId, disconnectActorId))
                .PacketName("UserSpotAuthReq")
                .Async<AuthRes>();
            await playA.Post("/spot/create")
                .Body(new CreateSpotReq(spotRid))
                .Async<CreateSpotRes>();
            await disconnectClient.Request(new JoinUserSpotActorReq(spotRid, disconnectActorId))
                .PacketName("JoinUserSpotActorReq")
                .Async<JoinUserSpotActorRes>();
            await disconnectClient.Close.Async();
        }

        var expectedDisconnectEvidence =
            $"spot-actor-disconnected|rid=play-a|spot={spotRid}|actor={disconnectActorId}";
        var playAAfterDisconnect = (await playA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([expectedDisconnectEvidence]))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            playAAfterDisconnect.Any(line => line.Contains(expectedDisconnectEvidence, StringComparison.Ordinal)),
            "SM-B6 expected disconnect evidence.");
        ZlinkStreamAssert.Ensure(
            playAAfterDisconnect.All(line => !line.Contains(
                $"spot-actor-left|rid=play-a|spot={spotRid}|actor={disconnectActorId}",
                StringComparison.Ordinal)),
            "SM-B6 disconnect incorrectly emitted leave evidence.");

        Console.WriteLine("operation SpotService.sm-b6 passed");
    }
}
