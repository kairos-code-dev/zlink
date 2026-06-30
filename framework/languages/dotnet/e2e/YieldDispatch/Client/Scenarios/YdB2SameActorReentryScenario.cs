using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdB2SameActorReentryScenario
{
    public static async Task RunAsync(
        IZlinkStreamConnector client,
        YieldActorScenarioContext actors)
    {
        var requestId = $"YD-B2-{Guid.NewGuid():N}";
        var yield = client.Request(new ActorYieldReq(requestId, 350))
            .PacketName("ActorYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        var fast = client.Request(new ActorFastReq(requestId, "b2-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.WhenAll(yield.AsTask(), fast.AsTask());

        var evidence = await client.Request(new YieldEvidenceReq(requestId))
            .PacketName("YieldEvidenceReq")
            .Metadata(YieldDispatchNames.TargetNodeRidMetadata, "play-a")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<YieldEvidenceReply>();
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "actor-yield-started",
            "actor-yield-released",
            "actor-yield-resumed",
            "actor-yield-completed",
            "actor-fast-started",
            "actor-fast-completed"
        ]);
    }
}