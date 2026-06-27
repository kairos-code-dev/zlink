using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdB3ActorJoinYieldScenario
{
    public static async Task RunAsync(
        IZlinkStreamConnector client,
        YieldActorScenarioContext actors)
    {
        var requestId = $"YD-B3-{Guid.NewGuid():N}";
        var join = client.Request(new ActorJoinYieldReq(requestId, actors.SpotRid))
            .PacketName("ActorJoinYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        var fast = client.Request(new ActorFastReq(requestId, "b3-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.WhenAll(join.AsTask(), fast.AsTask());

        var evidence = await client.Request(new YieldEvidenceReq(requestId))
            .PacketName("YieldEvidenceReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "actor-join-yield-started",
            "actor-join-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-join-yield-resumed",
            "actor-join-yield-completed"]);
    }
}
