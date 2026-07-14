using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD4MultipleActorBindingScenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint)
    {
        IZlinkStreamConnector? client = null;
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
                    var bound = await candidate.Request(new MultiBindReq("actor-sm-d4-x", "actor-sm-d4-y", "play-a"))
                        .PacketName("MultiBindReq")
                        .Async<MultiBindRes>();
                    ScenarioAssert.That(bound.BoundCount == 2, "SM-D4 expected two bound actors.");
                    client = candidate;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await candidate.DisposeAsync();
                    await Task.Delay(200);
                }
            }

            if (client is null)
                throw new InvalidOperationException(
                    last is null
                        ? "SM-D4 multi-bind did not become routable."
                        : $"SM-D4 multi-bind did not become routable. Last error: {last.Message}",
                    last);

            var x = await client.Request(new ActorPingReq("to-x"))
                .PacketName("ActorPingReq")
                .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-x")
                .Async<ActorPingRes>();
            var y = await client.Request(new ActorPingReq("to-y"))
                .PacketName("ActorPingReq")
                .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-y")
                .Async<ActorPingRes>();
            ScenarioAssert.That(x.ActorId == "actor-sm-d4-x" && x.Value == "to-x", "SM-D4 x relay mismatch.");
            ScenarioAssert.That(y.ActorId == "actor-sm-d4-y" && y.Value == "to-y", "SM-D4 y relay mismatch.");

            var xPushed = client.WaitFor<ActorPushNotify>()
                .Where(message => message.Payload.ActorId == "actor-sm-d4-x")
                .Async().AsTask();
            var xPushReply = await client.Request(new ActorPushReq("push-x"))
                .PacketName("ActorPushReq")
                .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-x")
                .Async<ActorPingRes>();
            var xNotify = await xPushed;
            ScenarioAssert.That(xPushReply.ActorId == "actor-sm-d4-x", "SM-D4 x push reply actor mismatch.");
            ScenarioAssert.That(xNotify.Payload.Value == "push-x", "SM-D4 x push payload mismatch.");

            var yPushed = client.WaitFor<ActorPushNotify>()
                .Where(message => message.Payload.ActorId == "actor-sm-d4-y")
                .Async().AsTask();
            var yPushReply = await client.Request(new ActorPushReq("push-y"))
                .PacketName("ActorPushReq")
                .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-y")
                .Async<ActorPingRes>();
            var yNotify = await yPushed;
            ScenarioAssert.That(yPushReply.ActorId == "actor-sm-d4-y", "SM-D4 y push reply actor mismatch.");
            ScenarioAssert.That(yNotify.Payload.Value == "push-y", "SM-D4 y push payload mismatch.");

            var actorIdLessRequestFailed = false;
            try
            {
                await client.Request(new ActorPingReq("missing-actor-id"))
                    .PacketName("ActorPingReq")
                    .Timeout(TimeSpan.FromSeconds(2))
                    .Async<ActorPingRes>();
            }
            catch
            {
                actorIdLessRequestFailed = true;
            }

            ScenarioAssert.That(
                actorIdLessRequestFailed,
                "SM-D4 expected actor-id-less request to fail with multiple bound actors.");
        }
        finally
        {
            if (client is not null) await client.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d4 passed");
    }
}
