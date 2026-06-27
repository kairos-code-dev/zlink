using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD8Scenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint)
    {
        const string actorId = "actor-sm-d8-reconnect";

        await using var first = await ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq(actorId, "stream reconnect", "play-a"));
        var pending = first.Request(new SlowActorPingReq("before-disconnect", 1000))
            .PacketName("SlowActorPingReq")
            .Timeout(TimeSpan.FromSeconds(10))
            .Async<ActorPingReply>()
            .AsTask();
        await Task.Delay(TimeSpan.FromMilliseconds(100));
        await first.Close.Async();
        await ExpectFailureAsync(pending, "SM-D8 expected pending request to fail after stream disconnect.");
        await Task.Delay(TimeSpan.FromMilliseconds(1200));

        await using var second = await ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq(actorId, "stream reconnect", "play-a"));
        var resumed = await second.Request(new ActorPingReq("after-reconnect"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(resumed.ActorId == actorId, "SM-D8 reconnected actor mismatch.");
        ScenarioAssert.That(resumed.NodeRid == "play-a", "SM-D8 reconnected node mismatch.");
        ScenarioAssert.That(resumed.Value == "after-reconnect", "SM-D8 reconnected value mismatch.");

        Console.WriteLine("operation SpotService.sm-d8 passed");
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
