using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class OtherActorProgressProbe
{
    public static async Task RunAsync(
        IZlinkStreamConnector client,
        AwaitActorScenarioContext actors)
    {
        var requestId = $"probe-B1-{Guid.NewGuid():N}";
        var pending = client.Request(new ActorAwaitReq(requestId, 350))
            .PacketName("ActorAwaitReq")
            .Metadata(AutomaticTurnDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorAwaitRes>();
        await Task.Delay(75);
        var fast = client.Request(new ActorFastReq(requestId, "b1-fast"))
            .PacketName("ActorFastReq")
            .Metadata(AutomaticTurnDispatchNames.ActorIdMetadata, actors.ActorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorAwaitRes>();
        await Task.WhenAll(pending.AsTask(), fast.AsTask());

        var evidence = await client.Request(new AwaitEvidenceReq(requestId))
            .PacketName("AwaitEvidenceReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "actor-await-started",
            "actor-await-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-await-resumed",
            "actor-await-completed"
        ]);
    }
}
