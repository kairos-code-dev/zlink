// Verifies TD-E3 Opposite Spot Join behavior.
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;
internal static class TdE3OppositeSpotJoinScenario
{
    public static async Task RunAsync(ExecutionTurnScenarioContext context)
    {
        var actors = await context.ActorsAsync();
        var spotA = $"td-e3-a-{Guid.NewGuid():N}";
        var spotB = $"td-e3-b-{Guid.NewGuid():N}";
        await context.EnsureSpotAsync(spotA, "play-a");
        await context.EnsureSpotAsync(spotB, "play-a");
        await context.EnsureActorInSpotAsync(actors.ActorA, spotA, "TD-E3-prepare-a");
        await context.EnsureActorInSpotAsync(actors.ActorB, spotB, "TD-E3-prepare-b");
        var requestA = ExecutionTurnScenarioContext.NewId("TD-E3-A");
        var requestB = ExecutionTurnScenarioContext.NewId("TD-E3-B");
        var moveA = context.ActorRequest(actors.ActorA,
                new ActorJoinAwaitReq(requestA, spotB))
            .Async<ActorAwaitRes>();
        var moveB = context.ActorRequest(actors.ActorB,
                new ActorJoinAwaitReq(requestB, spotA))
            .Async<ActorAwaitRes>();
        var replies = await Task.WhenAll(moveA.AsTask(), moveB.AsTask());
        ZlinkStreamAssert.Ensure(replies.All(reply => reply.Marker == "actor-join-deferred"),
            "TD-E3 opposite joins were not both deferred.");
        await Task.WhenAll(
            context.AssertJoinCompletionAsync(requestA, "actor-join", spotB, "TD-E3-A"),
            context.AssertJoinCompletionAsync(requestB, "actor-join", spotA, "TD-E3-B"));
    }
}
