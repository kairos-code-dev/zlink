using ToActorMessaging.Client.Support;
using ToActorMessaging.Shared;

namespace ToActorMessaging.Client.Scenarios;

internal static class TaB3RouteReconnectScenario
{
    public static async Task RunAsync(ToActorScenarioContext context)
    {
        const string actorId = "ta-b3";
        await context.EnsureActorAAsync(actorId);
        await context.CaptureAsync(actorId);
        await context.DisconnectCallerAsync();
        await Task.Delay(250);
        await context.AssertCachedFailureAsync(
            "TA-B3-disconnected-request", actorId, "RouteNotConnected");
        var disconnectedEvidence = await context.GetAllActorEvidenceAsync();
        ToActorScenarioContext.Require(
            disconnectedEvidence.All(item => item.Scenario != "TA-B3-disconnected-request"),
            "TA-B3 disconnected request unexpectedly reached an actor handler.");
        await context.ReconnectCallerAsync();
        await context.AssertCallWithRetryAsync(
            "TA-B3-recovered-request", actorId, "b3-recovered", "reply:b3-recovered");
        var recoveredEvidence = await context.GetAllActorEvidenceAsync();
        ToActorScenarioContext.Require(recoveredEvidence.Any(item => item is
        {
            Scenario: "TA-B3-recovered-request",
            Kind: "request",
            NodeRid: "actor-a",
            PacketName: nameof(ActorAsk)
        } && item.ActorId == actorId), "TA-B3 recovered request actor-owner evidence missing.");
    }
}
