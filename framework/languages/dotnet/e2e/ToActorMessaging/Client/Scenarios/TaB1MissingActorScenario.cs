using ToActorMessaging.Client.Support;

namespace ToActorMessaging.Client.Scenarios;

internal static class TaB1MissingActorScenario
{
    public static async Task RunAsync(ToActorScenarioContext context)
    {
        const string actorId = "missing-actor";
        await context.AssertRouteAbsentAsync(actorId);
        await context.AssertCallAsync("TA-B1-missing", actorId, "missing", "sent", send: true);
        await context.AssertFailureAsync(
            "TA-B1-missing-request", actorId, "ActorRouteNotFound", send: false);
        await context.AssertNoActorEvidenceAsync(actorId);
        await context.AssertRouteAbsentAsync(actorId);
    }
}
