using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD7StreamAuthenticationDispatchScenario
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
                    var authReply = await candidate.Request(new AuthReq("actor-sm-d7", "stream auth", "play-a"))
                        .PacketName("AuthReq")
                        .Async<AuthRes>();
                    ZlinkStreamAssert.Ensure(authReply.ActorId == "actor-sm-d7", "SM-D7 auth reply actor mismatch.");
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
                        ? "Actor auth did not become routable: actor-sm-d7"
                        : $"Actor auth did not become routable: actor-sm-d7. Last error: {last.Message}",
                    last);

            var reply = await client.Request(new ActorPingReq("auth-ok"))
                .PacketName("ActorPingReq")
                .Async<ActorPingRes>();
            ZlinkStreamAssert.Ensure(reply.ActorId == "actor-sm-d7", "SM-D7 relay actor mismatch.");
            ZlinkStreamAssert.Ensure(reply.Value == "auth-ok", "SM-D7 relay value mismatch.");
        }
        finally
        {
            if (client is not null) await client.DisposeAsync();
        }

        Console.WriteLine("operation SpotService.sm-d7 passed");
    }
}
