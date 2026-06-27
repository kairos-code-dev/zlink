using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client;

internal static class SpotActorRequestSupport
{
    public static async Task<IReadOnlyList<ActorPingReply>> RunBoundActorRequestsWithRetryAsync(
        string endpoint,
        AuthReq auth,
        IReadOnlyList<ActorPingReq> pings,
        string failureMessage)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                await using var client = CreateClient(endpoint);
                await client.Connect.Async();
                await client.Request(auth)
                    .PacketName("AuthReq")
                    .Async<AuthReply>();
                var replies = new List<ActorPingReply>(pings.Count);
                foreach (var ping in pings)
                {
                    var reply = await client.Request(ping)
                        .PacketName("ActorPingReq")
                        .Async<ActorPingReply>();
                    replies.Add(reply);
                }

                return replies.Count == 0
                    ? throw new InvalidOperationException("At least one actor ping is required.")
                    : replies;
            }
            catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
            {
                last = ex;
                await Task.Delay(200);
            }
        }

        throw new InvalidOperationException(
            last is null ? failureMessage : $"{failureMessage} Last error: {last.Message}",
            last);
    }

    public static async Task ExpectFailureAsync(Task task, string message)
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

    public static async Task<IZlinkStreamConnector> ConnectAndAuthWithRetryAsync(
        string endpoint,
        AuthReq auth,
        TimeSpan requestTimeout,
        Action<IZlinkStreamConnector>? configure = null,
        ZlinkStreamHeartbeatOptions? heartbeat = null)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var client = CreateClient(endpoint, requestTimeout, heartbeat);
            configure?.Invoke(client);
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
                await Task.Delay(500);
            }
        }

        throw new InvalidOperationException(
            last is null ? $"Actor auth did not become routable: {auth.ActorId}" : $"Actor auth did not become routable: {auth.ActorId}. Last error: {last.Message}",
            last);
    }

    public static IZlinkStreamConnector CreateClient(string endpoint)
        => CreateClient(
            endpoint,
            TimeSpan.FromSeconds(5),
            new ZlinkStreamHeartbeatOptions { Enabled = false });

    private static IZlinkStreamConnector CreateClient(
        string endpoint,
        TimeSpan requestTimeout,
        ZlinkStreamHeartbeatOptions? heartbeat)
    {
        ScenarioAssert.That(!string.IsNullOrWhiteSpace(endpoint), "session-a stream endpoint is required.");
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = requestTimeout,
            Heartbeat = heartbeat ?? new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024,
        });
    }
}
