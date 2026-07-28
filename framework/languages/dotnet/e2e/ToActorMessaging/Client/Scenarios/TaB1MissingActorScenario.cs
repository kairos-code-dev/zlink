// Verifies TA-B1 Missing Actor behavior.
using ToActorMessaging.Client.Support;

namespace ToActorMessaging.Client.Scenarios;

internal static class TaB1MissingActorScenario
{
    public static async Task RunAsync(ToActorScenarioContext context)
    {
        const string actorId = "missing-actor";
        await context.AssertRouteAbsentAsync(actorId);
        await context.AssertCallAsync(
            "TA-B1-missing",
            actorId,
            "missing",
            "sent",
            send: true,
            targetNodeRid: "actor-a",
            targetGeneration: 1);
        await context.AssertFailureAsync(
            "TA-B1-missing-request",
            actorId,
            "NotFound",
            send: false,
            targetNodeRid: "actor-a",
            targetGeneration: 1);
        await context.AssertNoActorEvidenceAsync(actorId);
        await context.AssertRouteAbsentAsync(actorId);
    }
}
