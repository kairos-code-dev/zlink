using YieldDispatch.Client.Support;
using YieldDispatch.Shared;

namespace YieldDispatch.Client.Scenarios;

internal sealed record YieldActorScenarioContext(string SpotRid, string ActorA, string ActorB)
{
    public static async Task<YieldActorScenarioContext> CreateAsync(YieldDispatchScenarioContext context)
    {
        var spotRid = $"yield-track-b-{Guid.NewGuid():N}";
        var actorA = $"actor-a-{Guid.NewGuid():N}";
        var actorB = $"actor-b-{Guid.NewGuid():N}";
        var bound = await context.Client.Request(new BindYieldActorsReq(spotRid, [actorA, actorB]))
            .PacketName("BindYieldActorsReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<BindYieldActorsReply>();
        ScenarioAssert.That(bound.Actors.Length == 2, "YD-B bind actor count mismatch.");

        return new YieldActorScenarioContext(spotRid, actorA, actorB);
    }
}
