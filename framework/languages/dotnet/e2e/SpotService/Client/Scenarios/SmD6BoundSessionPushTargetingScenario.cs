using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD6BoundSessionPushTargetingScenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint, string sessionBStreamEndpoint)
    {
        IZlinkStreamConnector? bound = null;
        IZlinkStreamConnector? unbound = null;
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
                    await client.Request(new AuthReq("actor-sm-d6", "bound", "play-a"))
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
                        ? "Actor auth did not become routable: actor-sm-d6"
                        : $"Actor auth did not become routable: actor-sm-d6. Last error: {last.Message}",
                    last);

            deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
            last = null;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var client = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
                {
                    Endpoint = new Uri(sessionBStreamEndpoint),
                    ConnectTimeout = TimeSpan.FromSeconds(5),
                    RequestTimeout = TimeSpan.FromSeconds(10),
                    Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    MaxReceivedMessages = 1024
                });
                try
                {
                    await client.Connect.Async();
                    await client.Request(new AuthReq("actor-sm-d6-shadow", "unbound", "play-b"))
                        .PacketName("AuthReq")
                        .Async<AuthRes>();
                    unbound = client;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await client.DisposeAsync();
                    await Task.Delay(500);
                }
            }

            if (unbound is null)
                throw new InvalidOperationException(
                    last is null
                        ? "Actor auth did not become routable: actor-sm-d6-shadow"
                        : $"Actor auth did not become routable: actor-sm-d6-shadow. Last error: {last.Message}",
                    last);

            var activeBound = bound;
            var activeUnbound = unbound;
            var pushed = activeBound.WaitFor<ActorPushNotify>().Async().AsTask();
            await activeBound.Request(new ActorPushReq("push-bound-only"))
                .PacketName("ActorPushReq")
                .Async<ActorPingRes>();
            var notify = await pushed;
            ScenarioAssert.That(notify.Payload.ActorId == "actor-sm-d6", "SM-D6 push actor mismatch.");
            ScenarioAssert.That(notify.Payload.Value == "push-bound-only", "SM-D6 push value mismatch.");
            await Task.Delay(200);
            ScenarioAssert.That(activeUnbound.ReceivedCount("ActorPushNotify") == 0,
                "SM-D6 unbound session received push.");
        }
        finally
        {
            if (bound is not null) await bound.DisposeAsync();
            if (unbound is not null) await unbound.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d6 passed");
    }
}
