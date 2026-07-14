using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD13HeartbeatRequestScenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint)
    {
        IZlinkStreamConnector? stream = null;
        try
        {
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
            Exception? last = null;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var client = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
                {
                    Endpoint = new Uri(sessionAStreamEndpoint),
                    ConnectTimeout = TimeSpan.FromSeconds(5),
                    RequestTimeout = TimeSpan.FromSeconds(5),
                    Heartbeat = new ZlinkStreamHeartbeatOptions
                    {
                        Enabled = true,
                        Interval = TimeSpan.FromMilliseconds(200),
                        Timeout = TimeSpan.FromSeconds(2)
                    },
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    MaxReceivedMessages = 1024
                });
                try
                {
                    await client.Connect.Async();
                    await client.Request(new AuthReq("actor-sm-d13", "heartbeat", "play-a"))
                        .PacketName("AuthReq")
                        .Async<AuthRes>();
                    stream = client;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await client.DisposeAsync();
                    await Task.Delay(500);
                }
            }

            if (stream is null)
                throw new InvalidOperationException(
                    last is null
                        ? "Actor auth did not become routable: actor-sm-d13"
                        : $"Actor auth did not become routable: actor-sm-d13. Last error: {last.Message}",
                    last);

            var activeStream = stream;
            await Task.Delay(600);
            ScenarioAssert.That(activeStream.IsConnected, "SM-D13 heartbeat-enabled stream disconnected.");
        }
        finally
        {
            if (stream is not null) await stream.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d13 passed");
    }
}
