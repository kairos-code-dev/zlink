using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdD4SessionRelayActorYieldScenario
{
    public static async Task RunAsync(
        IZlinkStreamConnector client,
        string sessionBStreamEndpoint,
        YieldActorScenarioContext actors)
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

        var requestId = $"YD-D4-{Guid.NewGuid():N}";
        var push = client.WaitFor<ActorPushNotify>()
            .Timeout(TimeSpan.FromSeconds(30))
            .Async()
            .AsTask();
        var reply = await client.Request(new ActorPushYieldReq(requestId, 250, "bound-session-push"))
            .PacketName("ActorPushYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldRes>();
        var notify = await push;
        ScenarioAssert.That(reply.ScenarioId == "YD-D4", "YD-D4 reply scenario mismatch.");
        ScenarioAssert.That(notify.Payload.ActorId == actors.ActorA, "YD-D4 push actor mismatch.");
        ScenarioAssert.That(notify.Payload.RequestId == requestId, "YD-D4 push request mismatch.");
        ScenarioAssert.That(notify.Payload.Value == "bound-session-push", "YD-D4 push value mismatch.");
        await Task.Delay(150);
        ScenarioAssert.That(unbound.ReceivedCount("ActorPushNotify") == 0,
            "YD-D4 unbound session received actor push.");

        var evidence = await client.Request(new YieldEvidenceReq(requestId))
            .PacketName("YieldEvidenceReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "actor-push-yield-started",
            "actor-push-yield-released",
            "actor-push-yield-resumed",
            "actor-push-yield-completed"
        ]);
    }
}