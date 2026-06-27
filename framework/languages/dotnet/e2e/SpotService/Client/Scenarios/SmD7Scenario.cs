using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD7Scenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint)
    {
        await using var client = await ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq("actor-sm-d7", "stream auth", "play-a"));
        var reply = await client.Request(new ActorPingReq("auth-ok"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(reply.ActorId == "actor-sm-d7", "SM-D7 relay actor mismatch.");
        ScenarioAssert.That(reply.Value == "auth-ok", "SM-D7 relay value mismatch.");

        Console.WriteLine("operation SpotService.sm-d7 passed");
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
                var reply = await client.Request(auth)
                    .PacketName("AuthReq")
                    .Async<AuthReply>();
                ScenarioAssert.That(reply.ActorId == auth.ActorId, "SM-D7 auth reply actor mismatch.");
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
}
