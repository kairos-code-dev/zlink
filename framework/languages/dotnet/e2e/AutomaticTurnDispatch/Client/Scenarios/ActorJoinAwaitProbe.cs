using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal static class ActorJoinAwaitProbe
{
    public static async Task RunAsync(
        IZlinkStreamConnector client,
        AwaitActorScenarioContext actors)
    {
        var requestId = $"probe-B3-{Guid.NewGuid():N}";
        var join = client.Request(new ActorJoinAwaitReq(requestId, actors.SpotRid))
            .PacketName("ActorJoinAwaitReq")
            .Metadata(AutomaticTurnDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorAwaitRes>();
        await Task.Delay(75);
        var fast = client.Request(new ActorFastReq(requestId, "b3-fast"))
            .PacketName("ActorFastReq")
            .Metadata(AutomaticTurnDispatchNames.ActorIdMetadata, actors.ActorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorAwaitRes>();
        await Task.WhenAll(join.AsTask(), fast.AsTask());

        var evidence = await client.Request(new AwaitEvidenceReq(requestId))
            .PacketName("AwaitEvidenceReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<AwaitEvidenceRes>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "actor-join-await-started",
            "actor-join-await-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-join-await-resumed",
            "actor-join-await-completed"
        ]);
    }
}
