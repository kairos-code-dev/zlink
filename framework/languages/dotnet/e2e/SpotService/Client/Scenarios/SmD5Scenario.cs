using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmD5Scenario
{
    public static async Task RunAsync(
        ZLinkHttpClient sessionA,
        string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-d5-notified-{Guid.NewGuid():N}";
        await using (var client = await ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq(actorId, "disconnect", "session-a")))
        {
            await client.Close.Async();
        }

        await EvidenceWait.ForAllAsync(
            sessionA,
            [$"entry-disconnected|rid=session-a|actor={actorId}"],
            "SM-D5 expected only the selected bound actor to receive disconnect notification.");

        Console.WriteLine("operation SpotService.sm-d5 passed");
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

}
