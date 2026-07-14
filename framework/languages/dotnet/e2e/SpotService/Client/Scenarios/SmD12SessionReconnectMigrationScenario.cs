using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD12SessionReconnectMigrationScenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint, string sessionBStreamEndpoint)
    {
        const string actorId = "actor-sm-d12-transfer";

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
                    await candidate.Request(new AuthReq(actorId, "api transfer", "play-a"))
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
                        : $"Actor auth did not become routable: {actorId}. Last error: {last.Message}", last);

            var firstReply = await first.Request(new ActorPingReq("before-transfer"))
                .PacketName("ActorPingReq")
                .Async<ActorPingRes>();
            ScenarioAssert.That(firstReply.ActorId == actorId, "SM-D12 first api actor mismatch.");
            ScenarioAssert.That(firstReply.NodeRid == "play-a", "SM-D12 first api node mismatch.");
            ScenarioAssert.That(firstReply.Seen == 1, "SM-D12 expected initial actor state.");
            await first.Close.Async();

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
                    await candidate.Request(new AuthReq(actorId, "api transfer", "play-a"))
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
                        : $"Actor auth did not become routable: {actorId}. Last error: {last.Message}", last);

            var snapshot = await second.Request(new SnapshotReq(actorId))
                .PacketName("SnapshotReq")
                .Async<SnapshotRes>();
            ScenarioAssert.That(snapshot.ActorId == actorId, "SM-D12 snapshot actor mismatch.");
            ScenarioAssert.That(snapshot.Seen == 1, "SM-D12 actor state was not preserved across apis.");

            var pushed = second.WaitFor<ActorPushNotify>().Async().AsTask();
            var resumed = await second.Request(new ActorPushReq("after-transfer"))
                .PacketName("ActorPushReq")
                .Async<ActorPingRes>();
            var notify = await pushed;
            ScenarioAssert.That(resumed.ActorId == actorId, "SM-D12 resumed actor mismatch.");
            ScenarioAssert.That(resumed.NodeRid == "play-a", "SM-D12 resumed node mismatch.");
            ScenarioAssert.That(resumed.Seen == 2, "SM-D12 resumed actor state mismatch.");
            ScenarioAssert.That(notify.Payload.ActorId == actorId, "SM-D12 resumed push actor mismatch.");
            ScenarioAssert.That(notify.Payload.Value == "after-transfer", "SM-D12 resumed push value mismatch.");
            ScenarioAssert.That(notify.Payload.Seen == 2, "SM-D12 resumed push state mismatch.");
        }
        finally
        {
            if (first is not null) await first.DisposeAsync();
            if (second is not null) await second.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d12 passed");
    }
}
