using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-D4 verifies public failure and server evidence for a missing request handler.
internal static class RlD4MissingRequestHandlerScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var failed = await consumer.Post("/profile/request/missing")
            .Body(new ProfileRequest("fast", "rl-d4-missing"))
            .SubmitRawAsync();
        ScenarioAssert.That(failed.Status >= 500, "RL-D4 expected public failure for missing request handler.");

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            new EvidenceWaitRequest(["dispatch-error|", "packet=MissingProfileRequest"], []),
            line => line.Contains("dispatch-error|", StringComparison.Ordinal)
                && line.Contains("packet=MissingProfileRequest", StringComparison.Ordinal),
            "RL-D4 dispatch-error marker missing.");

        Console.WriteLine("scenario RL-D4 passed");
    }
}
