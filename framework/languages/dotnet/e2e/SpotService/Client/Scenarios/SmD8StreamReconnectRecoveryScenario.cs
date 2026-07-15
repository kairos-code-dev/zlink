using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD8StreamReconnectRecoveryScenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint)
    {
        const string actorId = "actor-sm-d8-reconnect";

        IZlinkStreamConnector? first = null;
        IZlinkStreamConnector? second = null;
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
                    await candidate.Request(new AuthReq(actorId, "stream reconnect", "play-a"))
                        .PacketName("AuthReq")
                        .Async<AuthRes>();
                    first = candidate;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await candidate.DisposeAsync();
                    await Task.Delay(200);
                }
            }

            if (first is null)
                throw new InvalidOperationException(
                    last is null
                        ? $"Actor auth did not become routable: {actorId}"
                        : $"Actor auth did not become routable: {actorId}. Last error: {last.Message}",
                    last);

            var pending = first.Request(new SlowActorPingReq("before-disconnect", 1000))
                .PacketName("SlowActorPingReq")
                .Timeout(TimeSpan.FromSeconds(10))
                .Async<ActorPingRes>()
                .AsTask();
            await Task.Delay(TimeSpan.FromMilliseconds(100));
            await first.Close.Async();
            var pendingFailed = false;
            try
            {
                await pending;
            }
            catch
            {
                pendingFailed = true;
            }

            ZlinkStreamAssert.Ensure(pendingFailed, "SM-D8 expected pending request to fail after stream disconnect.");
            await Task.Delay(TimeSpan.FromMilliseconds(1200));

            deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
            last = null;
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
                    await candidate.Request(new AuthReq(actorId, "stream reconnect", "play-a"))
                        .PacketName("AuthReq")
                        .Async<AuthRes>();
                    second = candidate;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await candidate.DisposeAsync();
                    await Task.Delay(200);
                }
            }

            if (second is null)
                throw new InvalidOperationException(
                    last is null
                        ? $"Actor auth did not become routable: {actorId}"
                        : $"Actor auth did not become routable: {actorId}. Last error: {last.Message}",
                    last);

            var resumed = await second.Request(new ActorPingReq("after-reconnect"))
                .PacketName("ActorPingReq")
                .Async<ActorPingRes>();
            ZlinkStreamAssert.Ensure(resumed.ActorId == actorId, "SM-D8 reconnected actor mismatch.");
            ZlinkStreamAssert.Ensure(resumed.NodeRid == "play-a", "SM-D8 reconnected node mismatch.");
            ZlinkStreamAssert.Ensure(resumed.Value == "after-reconnect", "SM-D8 reconnected value mismatch.");
        }
        finally
        {
            if (first is not null) await first.DisposeAsync();
            if (second is not null) await second.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d8 passed");
    }
}
