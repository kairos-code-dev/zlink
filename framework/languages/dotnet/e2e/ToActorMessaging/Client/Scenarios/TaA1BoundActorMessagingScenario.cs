using ToActorMessaging.Client.Support;

namespace ToActorMessaging.Client.Scenarios;

internal static class TaA1BoundActorMessagingScenario
{
    public static async Task RunAsync(ToActorScenarioContext context)
    {
        const string actorId = "ta-a1";
        await context.EnsureActorAAsync(actorId);
        await using var bound = await context.ConnectAndBindAsync(context.Options.SessionAStreamEndpoint, actorId);
        await using var unbound = await context.ConnectAsync(context.Options.SessionBStreamEndpoint);
        await context.AssertBoundPushAsync(bound, unbound, "TA-A1", actorId, "BeforeNotify");
        await context.AssertCallAsync("TA-A1-send", actorId, "a1-send", "sent", send: true);
        await context.AssertCallAsync("TA-A1-request", actorId, "a1-request", "reply:a1-request", send: false);
        await context.AssertBoundPushAsync(bound, unbound, "TA-A1", actorId, "AfterNotify");

        var evidence = await context.GetAllActorEvidenceAsync();
        ToActorScenarioContext.Require(evidence.Any(item => item is
            { Scenario: "TA-A1-send", Kind: "send" }), "TA-A1 send evidence missing.");
        ToActorScenarioContext.Require(evidence.Any(item => item is
            { Scenario: "TA-A1-request", Kind: "request" }), "TA-A1 request evidence missing.");
        ToActorScenarioContext.Require(evidence.Any(item => item is
            { Scenario: "TA-A1", Kind: "bound-push", Value: "BeforeNotify" }),
            "TA-A1 BeforeNotify bound push evidence missing.");
        ToActorScenarioContext.Require(evidence.Any(item => item is
            { Scenario: "TA-A1", Kind: "bound-push", Value: "AfterNotify" }),
            "TA-A1 AfterNotify bound push evidence missing.");
    }
}
