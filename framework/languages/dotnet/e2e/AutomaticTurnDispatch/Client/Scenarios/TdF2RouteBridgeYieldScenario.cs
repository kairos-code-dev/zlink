// Verifies TD-F2 Route Bridge Yield behavior.
using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;
internal static class TdF2RouteBridgeYieldScenario
{
    public static async Task RunAsync(ExecutionTurnScenarioContext context)
    {
        var spot = $"td-f2-{Guid.NewGuid():N}";
        await context.EnsureSpotAsync(spot, "play-b");
        var requestId = ExecutionTurnScenarioContext.NewId("TD-F2");
        context.SendSpot(new AwaitMsg(requestId, 300, "TD-F2", "yield"), spot);
        await context.EvidenceAsync(requestId, "yield-released", "play-b");
        context.SendSpot(new ProbeMsg(requestId, "interleave-probe"), spot);
        await context.EvidenceAsync(requestId, "yield-completed", "play-b");
        var evidence = await context.EvidenceAsync(requestId, "probe-completed", "play-b");
        EvidenceOrder.ContainsExactRequestInOrder(evidence, requestId,
            ["yield-released", "probe-started", "probe-completed", "yield-resumed"]);
    }
}
