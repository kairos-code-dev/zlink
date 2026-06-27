using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD14Scenario
{
    public static async Task RunAsync(string sessionATlsStreamEndpoint)
    {
        await using var strict = CreateClient(
            sessionATlsStreamEndpoint,
            skipServerCertificateValidation: false);
        await ExpectFailureAsync(
            strict.Connect.Async().AsTask(),
            "SM-D14 expected strict TLS validation to reject the self-signed stream certificate.");

        await using var tls = CreateClient(
            sessionATlsStreamEndpoint,
            skipServerCertificateValidation: true);
        await tls.Connect.Async();
        await tls.Request(new AuthReq("actor-sm-d14-tls", "stream tls", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthReply>();

        var pushed = tls.WaitFor<ActorPushNotify>().Async().AsTask();
        var reply = await tls.Request(new ActorPushReq("tls-push"))
            .PacketName("ActorPushReq")
            .Async<ActorPingReply>();
        var notify = await pushed;
        ScenarioAssert.That(reply.ActorId == "actor-sm-d14-tls", "SM-D14 TLS actor reply mismatch.");
        ScenarioAssert.That(reply.NodeRid == "play-a", "SM-D14 TLS actor node mismatch.");
        ScenarioAssert.That(notify.Payload.ActorId == "actor-sm-d14-tls", "SM-D14 TLS push actor mismatch.");
        ScenarioAssert.That(notify.Payload.Value == "tls-push", "SM-D14 TLS push payload mismatch.");

        Console.WriteLine("operation SpotService.sm-d14 passed");
    }

    static IZlinkStreamConnector CreateClient(
        string endpoint,
        bool skipServerCertificateValidation)
    {
        ScenarioAssert.That(!string.IsNullOrWhiteSpace(endpoint), "session-a TLS stream endpoint is required.");
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024,
            SkipServerCertificateValidation = skipServerCertificateValidation,
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
