// Verifies TD-E2 User To User Spot Join behavior.
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;
internal static class TdE2UserToUserSpotJoinScenario
{
    public static async Task RunAsync(ExecutionTurnScenarioContext context)
    {
        var actors = await context.ActorsAsync();
        await context.EnsureActorInSpotAsync(actors.ActorA, actors.SpotRid, "TD-E2-prepare");
        var target = $"td-e2-target-{Guid.NewGuid():N}";
        await context.EnsureSpotAsync(target, "play-a");
        var reply = await context.ActorRequest(actors.ActorA,
                new ActorJoinAwaitReq(ExecutionTurnScenarioContext.NewId("TD-E2"), target))
            .Async<ActorAwaitRes>();
        ZlinkStreamAssert.Ensure(reply.Marker == "actor-join-completed", "TD-E2 user Spot join failed.");
    }
}
