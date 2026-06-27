using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-D3 verifies dispatch-error evidence for missing send handling.
internal static class RlD3DispatchErrorEvidenceScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var failed = await consumer.Post("/profile/request/missing")
            .Body(new ProfileRequest("fast", "rl-d3-missing"))
            .SubmitRawAsync();
        ScenarioAssert.That(failed.Status >= 500, "RL-D3 expected missing request handler failure.");

        await EvidenceWait.AnyProviderAsync(
            providerA,
            providerB,
            new EvidenceWaitRequest(
                ["dispatch-error|"],
                [["packet=MissingProfileRequest"]]),
            line => line.Contains("packet=MissingProfileRequest", StringComparison.Ordinal),
            "RL-D3 dispatch-error evidence did not include MissingProfileRequest.");

        Console.WriteLine("scenario RL-D3 passed");
    }
}
