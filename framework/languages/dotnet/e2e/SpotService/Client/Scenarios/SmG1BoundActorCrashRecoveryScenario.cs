using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

// Verifies recovery after the play-a process crashes.
internal static class SmG1BoundActorCrashRecoveryScenario
{
    public static async Task RunAsync(
        string playAUrl,
        string sessionAStreamEndpoint,
        string sessionBStreamEndpoint)
    {
        await using var playA = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(sessionAStreamEndpoint),
            RequestTimeout = TimeSpan.FromSeconds(30),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false }
        });
        await using var playB = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(sessionBStreamEndpoint),
            RequestTimeout = TimeSpan.FromSeconds(30),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false }
        });
        await playA.Connect.Async();
        await playB.Connect.Async();
        await playA.Request(new AuthReq("actor-sm-g1-crash", "crash-owner", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthRes>();
        await playB.Request(new AuthReq("actor-sm-g1-survivor", "survivor", "play-b"))
            .PacketName("AuthReq")
            .Async<AuthRes>();

        var beforeCrash = await playA.Request(new ActorPingReq("before-crash"))
            .PacketName("ActorPingReq")
            .Async<ActorPingRes>();
        ZlinkStreamAssert.Ensure(beforeCrash.NodeRid == "play-a", "SM-G1 play-a actor setup mismatch.");
        var beforeSurvivor = await playB.Request(new ActorPingReq("before-crash"))
            .PacketName("ActorPingReq")
            .Async<ActorPingRes>();
        ZlinkStreamAssert.Ensure(beforeSurvivor.NodeRid == "play-b", "SM-G1 play-b actor setup mismatch.");

        using var http = ZLinkHttpClient.Create(playAUrl)
            .Timeout(TimeSpan.FromSeconds(5))
            .Build();
        var response = await http.Post("/crash").AsyncRaw();
        ZlinkStreamAssert.Ensure(
            response.Status >= 200 && response.Status < 300,
            $"SM-G1 crash endpoint returned HTTP {response.Status}.");
        await Task.Delay(TimeSpan.FromMilliseconds(200));

        var afterCrashFailed = false;
        try
        {
            await playA.Request(new ActorPingReq("after-crash"))
                .PacketName("ActorPingReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<ActorPingRes>();
        }
        catch
        {
            afterCrashFailed = true;
        }

        ZlinkStreamAssert.Ensure(afterCrashFailed, "SM-G1 expected play-a actor request to fail after crash.");

        var survivor = await playB.Request(new ActorPingReq("after-crash"))
            .PacketName("ActorPingReq")
            .Async<ActorPingRes>();
        ZlinkStreamAssert.Ensure(survivor.ActorId == "actor-sm-g1-survivor", "SM-G1 survivor actor mismatch.");
        ZlinkStreamAssert.Ensure(survivor.NodeRid == "play-b", "SM-G1 survivor node mismatch.");

        await using var recovered = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(sessionBStreamEndpoint),
            RequestTimeout = TimeSpan.FromSeconds(30),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false }
        });
        await recovered.Connect.Async();
        // The crashed node's owner lease must expire before the claim can
        // be taken over; retry within the configured lease window.
        var reclaimDeadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(15);
        while (true)
        {
            try
            {
                await recovered.Request(new AuthReq("actor-sm-g1-crash", "recovered-on-play-b", "play-b"))
                    .PacketName("AuthReq")
                    .Async<AuthRes>();
                break;
            }
            catch (ZlinkStreamException) when (DateTimeOffset.UtcNow < reclaimDeadline)
            {
                await Task.Delay(500);
            }
        }
        var rebound = await recovered.Request(new ActorPingReq("rebound"))
            .PacketName("ActorPingReq")
            .Async<ActorPingRes>();
        ZlinkStreamAssert.Ensure(rebound.ActorId == "actor-sm-g1-crash", "SM-G1 rebound actor mismatch.");
        ZlinkStreamAssert.Ensure(rebound.NodeRid == "play-b", "SM-G1 rebound node mismatch.");

        Console.WriteLine("operation SpotService.sm-g1 passed");
    }
}
