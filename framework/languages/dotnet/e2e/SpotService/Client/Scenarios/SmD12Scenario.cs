using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD12Scenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint, string sessionBStreamEndpoint)
    {
        const string actorId = "actor-sm-d12-transfer";

        await using var first = await ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq(actorId, "api transfer", "play-a"));
        var firstReply = await first.Request(new ActorPingReq("before-transfer"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(firstReply.ActorId == actorId, "SM-D12 first api actor mismatch.");
        ScenarioAssert.That(firstReply.NodeRid == "play-a", "SM-D12 first api node mismatch.");
        ScenarioAssert.That(firstReply.Seen == 1, "SM-D12 expected initial actor state.");
        await first.Close.Async();

        await using var second = await ConnectAndAuthWithRetryAsync(
            sessionBStreamEndpoint,
            new AuthReq(actorId, "api transfer", "play-a"));
        var snapshot = await second.Request(new SnapshotReq(actorId))
            .PacketName("SnapshotReq")
            .Async<SnapshotReply>();
        ScenarioAssert.That(snapshot.ActorId == actorId, "SM-D12 snapshot actor mismatch.");
        ScenarioAssert.That(snapshot.Seen == 1, "SM-D12 actor state was not preserved across apis.");

        var pushed = second.WaitFor<ActorPushNotify>().Async().AsTask();
        var resumed = await second.Request(new ActorPushReq("after-transfer"))
            .PacketName("ActorPushReq")
            .Async<ActorPingReply>();
        var notify = await pushed;
        ScenarioAssert.That(resumed.ActorId == actorId, "SM-D12 resumed actor mismatch.");
        ScenarioAssert.That(resumed.NodeRid == "play-a", "SM-D12 resumed node mismatch.");
        ScenarioAssert.That(resumed.Seen == 2, "SM-D12 resumed actor state mismatch.");
        ScenarioAssert.That(notify.Payload.ActorId == actorId, "SM-D12 resumed push actor mismatch.");
        ScenarioAssert.That(notify.Payload.Value == "after-transfer", "SM-D12 resumed push value mismatch.");
        ScenarioAssert.That(notify.Payload.Seen == 2, "SM-D12 resumed push state mismatch.");

        Console.WriteLine("operation SpotService.sm-d12 passed");
    }

    static async Task<IZlinkStreamConnector> ConnectAndAuthWithRetryAsync(string endpoint, AuthReq auth)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var client = CreateClient(endpoint);
            try
            {
                await client.Connect.Async();
                await client.Request(auth)
                    .PacketName("AuthReq")
                    .Async<AuthReply>();
                return client;
            }
            catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
            {
                last = ex;
                await client.DisposeAsync();
                await Task.Delay(200);
            }
        }

        throw new InvalidOperationException(
            last is null ? $"Actor auth did not become routable: {auth.ActorId}" : $"Actor auth did not become routable: {auth.ActorId}. Last error: {last.Message}",
            last);
    }

    static IZlinkStreamConnector CreateClient(string endpoint)
    {
        ScenarioAssert.That(!string.IsNullOrWhiteSpace(endpoint), "stream endpoint is required.");
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
}
