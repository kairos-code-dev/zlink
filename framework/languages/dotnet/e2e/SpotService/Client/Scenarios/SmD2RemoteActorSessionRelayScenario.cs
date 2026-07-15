// Verifies SM-D2 Remote Actor Session Relay behavior.
using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmD2RemoteActorSessionRelayScenario
{
    public static async Task RunAsync(ZLinkHttpClient sessionA, string sessionAStreamEndpoint)
    {
        var controlDeadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? lastControlFailure = null;
        var controlReady = false;
        while (DateTimeOffset.UtcNow < controlDeadline)
        {
            try
            {
                var control = (await sessionA.Post("/channel/control-ping/play-b")
                    .Body(new ControlPingReq("sm-d2-play-b-ready"))
                    .Async<ControlPingRes>()).Body;
                if (control.NodeRid == "play-b")
                {
                    controlReady = true;
                    break;
                }
            }
            catch (Exception ex)
            {
                lastControlFailure = ex;
            }

            await Task.Delay(500);
        }

        if (!controlReady)
            throw new InvalidOperationException(
                lastControlFailure is null
                    ? "Control route did not become ready: play-b"
                    : $"Control route did not become ready: play-b. Last error: {lastControlFailure.Message}",
                lastControlFailure);

        IZlinkStreamConnector? remote = null;
        try
        {
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
            Exception? last = null;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var client = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
                {
                    Endpoint = new Uri(sessionAStreamEndpoint),
                    ConnectTimeout = TimeSpan.FromSeconds(5),
                    RequestTimeout = TimeSpan.FromSeconds(10),
                    Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    MaxReceivedMessages = 1024
                });
                try
                {
                    await client.Connect.Async();
                    await client.Request(new AuthReq("actor-sm-d2", "remote relay", "play-b"))
                        .PacketName("AuthReq")
                        .Async<AuthRes>();
                    remote = client;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await client.DisposeAsync();
                    await Task.Delay(500);
                }
            }

            if (remote is null)
                throw new InvalidOperationException(
                    last is null
                        ? "Actor auth did not become routable: actor-sm-d2"
                        : $"Actor auth did not become routable: actor-sm-d2. Last error: {last.Message}",
                    last);

            var activeRemote = remote;
            var remotePushed = activeRemote.WaitFor<ActorPushNotify>().Async().AsTask();
            var remoteReply = await activeRemote.Request(new ActorPushReq("push-remote"))
                .PacketName("ActorPushReq")
                .Async<ActorPingRes>();
            var remoteNotify = await remotePushed;
            ZlinkStreamAssert.Ensure(remoteReply.ActorId == "actor-sm-d2", "SM-D2 actor reply mismatch.");
            ZlinkStreamAssert.Ensure(remoteReply.NodeRid == "play-b", "SM-D2 remote node mismatch.");
            ZlinkStreamAssert.Ensure(remoteNotify.Payload.ActorId == "actor-sm-d2", "SM-D2 push actor mismatch.");
            ZlinkStreamAssert.Ensure(remoteNotify.Payload.Value == "push-remote", "SM-D2 push value mismatch.");
        }
        finally
        {
            if (remote is not null) await remote.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d2 passed");
    }
}
