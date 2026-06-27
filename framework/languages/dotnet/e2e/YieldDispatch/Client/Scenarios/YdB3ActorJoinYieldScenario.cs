using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdB3ActorJoinYieldScenario
{
    public static async Task RunAsync(
        YieldDispatchScenarioContext context,
        YieldActorScenarioContext actors)
    {
        var requestId = $"YD-B3-{Guid.NewGuid():N}";
        var join = context.Client.Request(new ActorJoinYieldReq(requestId, actors.SpotRid))
            .PacketName("ActorJoinYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        var fast = context.Client.Request(new ActorFastReq(requestId, "b3-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.WhenAll(join.AsTask(), fast.AsTask());

        var evidence = await context.ReadPlayEvidenceAsync(requestId, "play-a");
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "actor-join-yield-started",
            "actor-join-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-join-yield-resumed",
            "actor-join-yield-completed"]);
    }
}
