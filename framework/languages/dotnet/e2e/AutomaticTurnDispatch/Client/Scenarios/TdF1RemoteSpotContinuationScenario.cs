// Verifies TD-F1 Remote Spot Continuation behavior.
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;
internal static class TdF1RemoteSpotContinuationScenario
{
    public static async Task RunAsync(ExecutionTurnScenarioContext context)
    {
        var owner = await context.SpotAsync();
        var target = $"td-f1-target-{Guid.NewGuid():N}";
        await context.EnsureSpotAsync(target, "play-b");
        var reply = await context.SpotRequest(owner,
                new RemoteSpotAwaitReq(ExecutionTurnScenarioContext.NewId("TD-F1"), target, 100))
            .Async<AutomaticTurnDispatchRes>();
        ZlinkStreamAssert.Ensure(reply.NodeRid == "play-a", "TD-F1 continuation did not return to the caller node.");
    }
}
