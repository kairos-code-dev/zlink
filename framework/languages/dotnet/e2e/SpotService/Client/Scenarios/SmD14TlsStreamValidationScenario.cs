using SpotService.Client.Support;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD14TlsStreamValidationScenario
{
    public static async Task RunAsync(string sessionATlsStreamEndpoint)
    {
        await using var strict = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(sessionATlsStreamEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024,
            SkipServerCertificateValidation = false
        });
        var strictTlsRejected = false;
        try
        {
            await strict.Connect.Async();
        }
        catch
        {
            strictTlsRejected = true;
        }

        ScenarioAssert.That(
            strictTlsRejected,
            "SM-D14 expected strict TLS validation to reject the self-signed stream certificate.");

        await using var tls = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(sessionATlsStreamEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024,
            SkipServerCertificateValidation = true
        });
        await tls.Connect.Async();
        await tls.Request(new AuthReq("actor-sm-d14-tls", "stream tls", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthRes>();

        var pushed = tls.WaitFor<ActorPushNotify>().Async().AsTask();
        var reply = await tls.Request(new ActorPushReq("tls-push"))
            .PacketName("ActorPushReq")
            .Async<ActorPingRes>();
        var notify = await pushed;
        ScenarioAssert.That(reply.ActorId == "actor-sm-d14-tls", "SM-D14 TLS actor reply mismatch.");
        ScenarioAssert.That(reply.NodeRid == "play-a", "SM-D14 TLS actor node mismatch.");
        ScenarioAssert.That(notify.Payload.ActorId == "actor-sm-d14-tls", "SM-D14 TLS push actor mismatch.");
        ScenarioAssert.That(notify.Payload.Value == "tls-push", "SM-D14 TLS push payload mismatch.");

        Console.WriteLine("operation SpotService.sm-d14 passed");
    }
}
