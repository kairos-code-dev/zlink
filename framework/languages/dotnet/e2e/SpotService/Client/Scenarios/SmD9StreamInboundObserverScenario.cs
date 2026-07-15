using System.Collections.Concurrent;
using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD9StreamInboundObserverScenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint)
    {
        var observed = new ConcurrentQueue<string>();
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
                    Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    MaxReceivedMessages = 1024
                });
                client.ObserveInbound((observation, cancellationToken) =>
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    observed.Enqueue(observation.Name);
                    return ValueTask.CompletedTask;
                });
                try
                {
                    await client.Connect.Async();
                    await client.Request(new AuthReq("actor-sm-d9", "observer", "play-a"))
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
                        ? "Actor auth did not become routable: actor-sm-d9"
                        : $"Actor auth did not become routable: actor-sm-d9. Last error: {last.Message}",
                    last);

            var activeStream = stream;
            await activeStream.Request(new ActorPingReq("observer-1"))
                .PacketName("ActorPingReq")
                .Async<ActorPingRes>();
            await activeStream.Request(new ActorPingReq("observer-2"))
                .PacketName("ActorPingReq")
                .Async<ActorPingRes>();
            ZlinkStreamAssert.Ensure(observed.Count >= 2, "SM-D9 inbound observer did not observe stream replies.");
        }
        finally
        {
            if (stream is not null) await stream.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d9 passed");
    }
}
