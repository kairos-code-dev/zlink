using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD10BoundedSessionBackpressureScenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint, string sessionBStreamEndpoint)
    {
        IZlinkStreamConnector? congested = null;
        IZlinkStreamConnector? isolated = null;
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
                    MaxReceivedMessages = 1
                });
                try
                {
                    await candidate.Connect.Async();
                    await candidate.Request(new AuthReq("actor-sm-d10-congested", "stream backpressure", "play-a"))
                        .PacketName("AuthReq")
                        .Async<AuthRes>();
                    congested = candidate;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await candidate.DisposeAsync();
                    await Task.Delay(200);
                }
            }

            if (congested is null)
                throw new InvalidOperationException(
                    last is null
                        ? "Actor auth did not become routable: actor-sm-d10-congested"
                        : $"Actor auth did not become routable: actor-sm-d10-congested. Last error: {last.Message}",
                    last);

            deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
            last = null;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var candidate = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
                {
                    Endpoint = new Uri(sessionBStreamEndpoint),
                    ConnectTimeout = TimeSpan.FromSeconds(5),
                    RequestTimeout = TimeSpan.FromSeconds(5),
                    Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    MaxReceivedMessages = 1024
                });
                try
                {
                    await candidate.Connect.Async();
                    await candidate.Request(new AuthReq("actor-sm-d10-isolated", "stream backpressure peer", "play-b"))
                        .PacketName("AuthReq")
                        .Async<AuthRes>();
                    isolated = candidate;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await candidate.DisposeAsync();
                    await Task.Delay(200);
                }
            }

            if (isolated is null)
                throw new InvalidOperationException(
                    last is null
                        ? "Actor auth did not become routable: actor-sm-d10-isolated"
                        : $"Actor auth did not become routable: actor-sm-d10-isolated. Last error: {last.Message}",
                    last);

            for (var index = 0; index < 8; index++)
            {
                var reply = await congested.Request(new ActorPushReq($"burst-{index}"))
                    .PacketName("ActorPushReq")
                    .Async<ActorPingRes>();
                ScenarioAssert.That(reply.ActorId == "actor-sm-d10-congested",
                    "SM-D10 congested reply actor mismatch.");
            }

            ScenarioAssert.That(
                congested.ReceivedCount("ActorPushNotify") <= 1,
                "SM-D10 expected bounded received-message queue for congested session.");
            var retained = await congested.WaitFor<ActorPushNotify>()
                .Where(message => message.Payload.ActorId == "actor-sm-d10-congested")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async();
            ScenarioAssert.That(retained.Payload.Value == "burst-7",
                "SM-D10 expected the newest congested push to be retained.");

            var stillAlive = await congested.Request(new ActorPingReq("after-backpressure"))
                .PacketName("ActorPingReq")
                .Async<ActorPingRes>();
            ScenarioAssert.That(stillAlive.ActorId == "actor-sm-d10-congested",
                "SM-D10 congested session stopped routing.");
            ScenarioAssert.That(stillAlive.Value == "after-backpressure", "SM-D10 congested session reply mismatch.");

            var isolatedPush = isolated.WaitFor<ActorPushNotify>().Async().AsTask();
            var isolatedReply = await isolated.Request(new ActorPushReq("isolated-push"))
                .PacketName("ActorPushReq")
                .Async<ActorPingRes>();
            var isolatedNotify = await isolatedPush;
            ScenarioAssert.That(isolatedReply.ActorId == "actor-sm-d10-isolated",
                "SM-D10 isolated reply actor mismatch.");
            ScenarioAssert.That(isolatedNotify.Payload.ActorId == "actor-sm-d10-isolated",
                "SM-D10 isolated push actor mismatch.");
            ScenarioAssert.That(isolatedNotify.Payload.Value == "isolated-push",
                "SM-D10 isolated session push mismatch.");
        }
        finally
        {
            if (congested is not null) await congested.DisposeAsync();
            if (isolated is not null) await isolated.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d10 passed");
    }
}
