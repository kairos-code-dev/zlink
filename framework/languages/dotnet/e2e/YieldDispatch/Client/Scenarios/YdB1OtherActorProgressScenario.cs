using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal static class YdB1OtherActorProgressScenario
{
    public static async Task RunAsync(
        YieldDispatchScenarioContext context,
        YieldActorScenarioContext actors)
    {
        var requestId = $"YD-B1-{Guid.NewGuid():N}";
        var yield = context.Client.Request(new ActorYieldReq(requestId, 350))
            .PacketName("ActorYieldReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorA)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.Delay(75);
        var fast = context.Client.Request(new ActorFastReq(requestId, "b1-fast"))
            .PacketName("ActorFastReq")
            .Metadata(YieldDispatchNames.ActorIdMetadata, actors.ActorB)
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<ActorYieldReply>();
        await Task.WhenAll(yield.AsTask(), fast.AsTask());

        var evidence = await context.ReadPlayEvidenceAsync(requestId, "play-a");
        ScenarioAssert.ContainsExactRequestInOrder(evidence.Evidence, requestId, [
            "actor-yield-started",
            "actor-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed"]);
    }
}
