using ToActorMessaging.Client.Support;

namespace ToActorMessaging.Client.Scenarios;

internal static class TaA3LateBindScenario
{
    public static async Task RunAsync(ToActorScenarioContext context)
    {
        const string actorId = "ta-a3";
        await context.EnsureActorAAsync(actorId);
        await context.AssertCallAsync("TA-A3-before-bind-send", actorId, "a3-before-send", "sent", send: true);
        await context.AssertCallAsync(
            "TA-A3-before-bind-request", actorId, "a3-before-request", "reply:a3-before-request", send: false);
        await context.AssertBoundPushFailureAsync(
            actorB: false, "TA-A3-before-bind", actorId, "must-remain-unbound");
        await using var bound = await context.ConnectAndBindAsync(context.Options.SessionBStreamEndpoint, actorId);
        await context.AssertCallAsync("TA-A3-after-bind-send", actorId, "a3-after-send", "sent", send: true);
        await context.AssertCallAsync(
            "TA-A3-after-bind-request", actorId, "a3-after-request", "reply:a3-after-request", send: false);
        await context.AssertBoundPushAsync(bound, null, "TA-A3", actorId, "LateBindNotify");

        var evidence = await context.GetAllActorEvidenceAsync();
        foreach (var (scenario, kind) in new[]
                 {
                     ("TA-A3-before-bind-send", "send"),
                     ("TA-A3-before-bind-request", "request"),
                     ("TA-A3-after-bind-send", "send"),
                     ("TA-A3-after-bind-request", "request"),
                     ("TA-A3", "bound-push")
                 })
            ToActorScenarioContext.Require(
                evidence.Any(item => item.Scenario == scenario && item.Kind == kind),
                $"{scenario} {kind} evidence missing.");
    }
}
