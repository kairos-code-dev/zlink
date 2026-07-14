using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class SessionRelayActorAwaitProbe
{
    public static async Task RunAsync(
        IZlinkStreamConnector client,
        string sessionBStreamEndpoint,
        AwaitActorScenarioContext actors)
    {
        await using var unbound = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(sessionBStreamEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(60),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024
        });
        await unbound.Connect.Async();

        var requestId = $"probe-D4-{Guid.NewGuid():N}";
        var push = client.WaitFor<ActorPushNotify>()
            .Timeout(TimeSpan.FromSeconds(30))
            .Async()
            .AsTask();
        var reply = await client.Request(new ActorPushAwaitReq(requestId, 250, "bound-session-push"))
            .PacketName("ActorPushAwaitReq")
            .Metadata(AutomaticTurnDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorAwaitRes>();
        var notify = await push;
        ScenarioAssert.That(reply.ScenarioId == "probe-D4", "probe-D4 reply scenario mismatch.");
        ScenarioAssert.That(notify.Payload.ActorId == actors.ActorA, "probe-D4 push actor mismatch.");
        ScenarioAssert.That(notify.Payload.RequestId == requestId, "probe-D4 push request mismatch.");
        ScenarioAssert.That(notify.Payload.Value == "bound-session-push", "probe-D4 push value mismatch.");
        await Task.Delay(150);
        ScenarioAssert.That(unbound.ReceivedCount("ActorPushNotify") == 0,
            "probe-D4 unbound session received actor push.");

        var evidence = await client.Request(new AwaitEvidenceReq(requestId))
            .PacketName("AwaitEvidenceReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "actor-push-await-started",
            "actor-push-await-released",
            "actor-push-await-resumed",
            "actor-push-await-completed"
        ]);
    }
}
