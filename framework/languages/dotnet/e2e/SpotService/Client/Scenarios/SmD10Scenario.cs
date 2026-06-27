using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD10Scenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint, string sessionBStreamEndpoint)
    {
        await using var congested = await ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq("actor-sm-d10-congested", "stream backpressure", "play-a"),
            maxReceivedMessages: 1);
        await using var isolated = await ConnectAndAuthWithRetryAsync(
            sessionBStreamEndpoint,
            new AuthReq("actor-sm-d10-isolated", "stream backpressure peer", "play-b"));

        for (var index = 0; index < 8; index++)
        {
            var reply = await congested.Request(new ActorPushReq($"burst-{index}"))
                .PacketName("ActorPushReq")
                .Async<ActorPingReply>();
            ScenarioAssert.That(reply.ActorId == "actor-sm-d10-congested", "SM-D10 congested reply actor mismatch.");
        }

        ScenarioAssert.That(
            congested.ReceivedCount("ActorPushNotify") <= 1,
            "SM-D10 expected bounded received-message queue for congested session.");
        var retained = await congested.WaitFor<ActorPushNotify>()
            .Where(message => message.Payload.ActorId == "actor-sm-d10-congested")
            .Timeout(TimeSpan.FromSeconds(2))
            .Async();
        ScenarioAssert.That(retained.Payload.Value == "burst-7", "SM-D10 expected the newest congested push to be retained.");

        var stillAlive = await congested.Request(new ActorPingReq("after-backpressure"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(stillAlive.ActorId == "actor-sm-d10-congested", "SM-D10 congested session stopped routing.");
        ScenarioAssert.That(stillAlive.Value == "after-backpressure", "SM-D10 congested session reply mismatch.");

        var isolatedPush = isolated.WaitFor<ActorPushNotify>().Async().AsTask();
        var isolatedReply = await isolated.Request(new ActorPushReq("isolated-push"))
            .PacketName("ActorPushReq")
            .Async<ActorPingReply>();
        var isolatedNotify = await isolatedPush;
        ScenarioAssert.That(isolatedReply.ActorId == "actor-sm-d10-isolated", "SM-D10 isolated reply actor mismatch.");
        ScenarioAssert.That(isolatedNotify.Payload.ActorId == "actor-sm-d10-isolated", "SM-D10 isolated push actor mismatch.");
        ScenarioAssert.That(isolatedNotify.Payload.Value == "isolated-push", "SM-D10 isolated session push mismatch.");

        Console.WriteLine("operation SpotService.sm-d10 passed");
    }

    static async Task<IZlinkStreamConnector> ConnectAndAuthWithRetryAsync(
        string endpoint,
        AuthReq auth,
        int maxReceivedMessages = 1024)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var client = CreateClient(endpoint, maxReceivedMessages);
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

    static IZlinkStreamConnector CreateClient(string endpoint, int maxReceivedMessages)
    {
        ScenarioAssert.That(!string.IsNullOrWhiteSpace(endpoint), "stream endpoint is required.");
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = maxReceivedMessages,
        });
    }
}
