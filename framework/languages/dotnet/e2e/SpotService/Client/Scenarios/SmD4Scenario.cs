using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD4Scenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint)
    {
        await using var client = await ConnectAndBindWithRetryAsync(sessionAStreamEndpoint);

        var x = await client.Request(new ActorPingReq("to-x"))
            .PacketName("ActorPingReq")
            .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-x")
            .Async<ActorPingReply>();
        var y = await client.Request(new ActorPingReq("to-y"))
            .PacketName("ActorPingReq")
            .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-y")
            .Async<ActorPingReply>();
        ScenarioAssert.That(x.ActorId == "actor-sm-d4-x" && x.Value == "to-x", "SM-D4 x relay mismatch.");
        ScenarioAssert.That(y.ActorId == "actor-sm-d4-y" && y.Value == "to-y", "SM-D4 y relay mismatch.");

        var xPushed = client.WaitFor<ActorPushNotify>()
            .Where(message => message.Payload.ActorId == "actor-sm-d4-x")
            .Async().AsTask();
        var xPushReply = await client.Request(new ActorPushReq("push-x"))
            .PacketName("ActorPushReq")
            .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-x")
            .Async<ActorPingReply>();
        var xNotify = await xPushed;
        ScenarioAssert.That(xPushReply.ActorId == "actor-sm-d4-x", "SM-D4 x push reply actor mismatch.");
        ScenarioAssert.That(xNotify.Payload.Value == "push-x", "SM-D4 x push payload mismatch.");

        var yPushed = client.WaitFor<ActorPushNotify>()
            .Where(message => message.Payload.ActorId == "actor-sm-d4-y")
            .Async().AsTask();
        var yPushReply = await client.Request(new ActorPushReq("push-y"))
            .PacketName("ActorPushReq")
            .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-y")
            .Async<ActorPingReply>();
        var yNotify = await yPushed;
        ScenarioAssert.That(yPushReply.ActorId == "actor-sm-d4-y", "SM-D4 y push reply actor mismatch.");
        ScenarioAssert.That(yNotify.Payload.Value == "push-y", "SM-D4 y push payload mismatch.");

        await ExpectFailureAsync(
            client.Request(new ActorPingReq("missing-actor-id"))
                .PacketName("ActorPingReq")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<ActorPingReply>().AsTask(),
            "SM-D4 expected actor-id-less request to fail with multiple bound actors.");

        Console.WriteLine("operation SpotService.sm-d4 passed");
    }

    static async Task<IZlinkStreamConnector> ConnectAndBindWithRetryAsync(string endpoint)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var client = CreateClient(endpoint);
            try
            {
                await client.Connect.Async();
                var bound = await client.Request(new MultiBindReq("actor-sm-d4-x", "actor-sm-d4-y", "play-a"))
                    .PacketName("MultiBindReq")
                    .Async<MultiBindReply>();
                ScenarioAssert.That(bound.BoundCount == 2, "SM-D4 expected two bound actors.");
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
            last is null ? "SM-D4 multi-bind did not become routable." : $"SM-D4 multi-bind did not become routable. Last error: {last.Message}",
            last);
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

    static async Task ExpectFailureAsync(Task task, string message)
    {
        try
        {
            await task;
        }
        catch
        {
            return;
        }

        throw new InvalidOperationException(message);
    }
}
