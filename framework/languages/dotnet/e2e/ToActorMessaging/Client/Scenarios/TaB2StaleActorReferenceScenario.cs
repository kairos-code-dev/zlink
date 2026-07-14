using ToActorMessaging.Client.Support;
using ToActorMessaging.Shared;

namespace ToActorMessaging.Client.Scenarios;

internal static class TaB2StaleActorReferenceScenario
{
    public static async Task RunAsync(ToActorScenarioContext context)
    {
        const string actorId = "ta-b2";
        await context.PostActorAWithRetryAsync($"/actors/{actorId}/ensure");
        var staleRef = await context.CaptureAsync(actorId);
        await context.DestroyActorAAsync(actorId, "TA-B2");
        await context.PostActorAWithRetryAsync($"/actors/{actorId}/ensure");

        await context.AssertCachedFailureAsync("TA-B2-stale-request", actorId, "ActorLocationStale");
        var afterStale = await context.GetAllActorEvidenceAsync();
        ToActorScenarioContext.Require(afterStale.All(item => item.Scenario != "TA-B2-stale-request"),
            "TA-B2 stale generation unexpectedly reached an actor handler.");
        var freshRef = await context.CaptureAsync(actorId);
        ToActorScenarioContext.Require(
            freshRef.NodeRid == staleRef.NodeRid && freshRef.Generation > staleRef.Generation,
            $"TA-B2 expected a newer generation on the same owner. stale={staleRef}, fresh={freshRef}");
        await context.AssertCallWithRetryAsync(
            "TA-B2-fresh-request", actorId, "b2-fresh", "reply:b2-fresh");
        var afterFresh = await context.GetAllActorEvidenceAsync();
        ToActorScenarioContext.Require(afterFresh.Any(item => item is
        {
            Scenario: "TA-B2-fresh-request",
            Kind: "request",
            NodeRid: "actor-a",
            PacketName: nameof(ActorAsk)
        } && item.ActorId == actorId && item.Generation == freshRef.Generation),
            "TA-B2 fresh request generation/owner evidence missing.");
    }
}
