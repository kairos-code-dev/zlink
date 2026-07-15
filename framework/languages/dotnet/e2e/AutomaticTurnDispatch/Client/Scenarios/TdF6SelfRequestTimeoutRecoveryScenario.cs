// Verifies TD-F6 Self Request Timeout Recovery behavior.
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;
internal static class TdF6SelfRequestTimeoutRecoveryScenario
{
    public static async Task RunAsync(ExecutionTurnScenarioContext context)
    {
        var spot = await context.SpotAsync();
        var requestId = ExecutionTurnScenarioContext.NewId("TD-F6");
        context.SendSpot(new SelfCycleMsg(requestId, 150), spot);
        await context.EvidenceAsync(requestId, "self-cycle-timed-out");
        var reply = await context.SpotRequest(spot, new ProbeReq(requestId, "post-cycle"))
            .Async<AutomaticTurnDispatchRes>();
        ZlinkStreamAssert.Ensure(reply.Marker == "post-cycle", "TD-F6 Spot did not recover after timeout.");
    }
}
