// Verifies SM-D11 Stream And Route Request behavior.
using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmD11StreamAndRouteRequestScenario
{
    public static async Task RunAsync(ZLinkHttpClient sessionA, string sessionAStreamEndpoint)
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
                    Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    MaxReceivedMessages = 1024
                });
                try
                {
                    await client.Connect.Async();
                    await client.Request(new AuthReq("actor-sm-d11", "stream and channel", "play-a"))
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
                        ? "Actor auth did not become routable: actor-sm-d11"
                        : $"Actor auth did not become routable: actor-sm-d11. Last error: {last.Message}",
                    last);

            var activeStream = stream;
            var streamReply = await activeStream.Request(new ActorPingReq("stream-side"))
                .PacketName("ActorPingReq")
                .Async<ActorPingRes>();
            ZlinkStreamAssert.Ensure(streamReply.ActorId == "actor-sm-d11", "SM-D11 stream request actor mismatch.");
        }
        finally
        {
            if (stream is not null) await stream.DisposeAsync();
        }

        var channelReply = (await sessionA.Post("/channel/control-ping/play-a")
            .Body(new ControlPingReq("channel-side"))
            .Async<ControlPingRes>()).Body;
        ZlinkStreamAssert.Ensure(channelReply.NodeRid == "play-a", "SM-D11 channel request node mismatch.");
        ZlinkStreamAssert.Ensure(channelReply.Value == "channel-side", "SM-D11 channel reply value mismatch.");
        Console.WriteLine("operation SpotService.sm-d11 passed");
    }
}
