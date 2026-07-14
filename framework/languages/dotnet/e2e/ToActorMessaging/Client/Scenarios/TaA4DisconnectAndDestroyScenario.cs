using ToActorMessaging.Client.Support;

namespace ToActorMessaging.Client.Scenarios;

internal static class TaA4DisconnectAndDestroyScenario
{
    public static async Task RunAsync(ToActorScenarioContext context)
    {
        const string actorId = "ta-a4";
        await context.EnsureActorAAsync(actorId);
        await context.CaptureAsync(actorId);
        await using (var bound = await context.ConnectAndBindAsync(context.Options.SessionAStreamEndpoint, actorId))
        {
            await context.AssertBoundPushAsync(bound, null, "TA-A4", actorId, "BeforeDisconnect");
            await bound.Close.Async();
        }
        await context.WaitForSessionAEvidenceAsync(
            "session-disconnected|", "TA-A4 session disconnect evidence missing.");
        await context.AssertBoundPushFailureAsync(actorB: false, "TA-A4", actorId, "AfterDisconnect");
        await context.AssertCallWithRetryAsync(
            "TA-A4-disconnected-request", actorId, "a4-request", "reply:a4-request");
        await context.AssertCallAsync("TA-A4-disconnected-send", actorId, "a4-send", "sent", send: true);
        await context.DestroyActorAAsync(actorId, "TA-A4");
        await context.AssertCachedFailureWithRetryAsync(
            "TA-A4-destroyed-request", actorId, "ActorRouteNotFound");

        var evidence = await context.GetAllActorEvidenceAsync();
        foreach (var (scenario, kind) in new[]
                 {
                     ("TA-A4-disconnected-send", "send"),
                     ("TA-A4-disconnected-request", "request"),
                     ("TA-A4", "destroy")
                 })
            ToActorScenarioContext.Require(
                evidence.Any(item => item.Scenario == scenario && item.Kind == kind),
                $"{scenario} {kind} evidence missing.");
    }
}
