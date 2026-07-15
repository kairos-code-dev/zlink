using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmD1LocalActorSessionRelayScenario
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
                var control = (await sessionA.Post("/channel/control-ping/play-a")
                    .Body(new ControlPingReq("sm-d1-play-a-ready"))
                    .Async<ControlPingRes>()).Body;
                if (control.NodeRid == "play-a")
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
                    ? "Control route did not become ready: play-a"
                    : $"Control route did not become ready: play-a. Last error: {lastControlFailure.Message}",
                lastControlFailure);

        IZlinkStreamConnector? bound = null;
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
                    await client.Request(new AuthReq("actor-sm-d1", "local relay", "play-a"))
                        .PacketName("AuthReq")
                        .Async<AuthRes>();
                    bound = client;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await client.DisposeAsync();
                    await Task.Delay(500);
                }
            }

            if (bound is null)
                throw new InvalidOperationException(
                    last is null
                        ? "Actor auth did not become routable: actor-sm-d1"
                        : $"Actor auth did not become routable: actor-sm-d1. Last error: {last.Message}",
                    last);

            var activeBound = bound;
            var pushed = activeBound.WaitFor<ActorPushNotify>().Async().AsTask();
            var reply = await activeBound.Request(new ActorPushReq("push-local"))
                .PacketName("ActorPushReq")
                .Async<ActorPingRes>();
            var notify = await pushed;
            ZlinkStreamAssert.Ensure(reply.ActorId == "actor-sm-d1", "SM-D1 actor reply mismatch.");
            ZlinkStreamAssert.Ensure(notify.Payload.ActorId == "actor-sm-d1", "SM-D1 push actor mismatch.");
            ZlinkStreamAssert.Ensure(notify.Payload.Value == "push-local", "SM-D1 push value mismatch.");
        }
        finally
        {
            if (bound is not null) await bound.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d1 passed");
    }
}
