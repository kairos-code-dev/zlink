using ResilienceLifecycle.Client.Support;
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

        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
            var request = new EvidenceWaitRequest(["dispatch-error|"], [["packet=MissingProfileRequest"]]);
            var waitA = providerA.Post("/evidence/wait").Body(request).SubmitAsync<string[]>(timeout.Token).AsTask();
            var waitB = providerB.Post("/evidence/wait").Body(request).SubmitAsync<string[]>(timeout.Token).AsTask();
            var completed = await Task.WhenAny(waitA, waitB);
            var evidence = (await completed).Body;
            timeout.Cancel();
            ScenarioAssert.That(
                evidence.Any(line => line.Contains("packet=MissingProfileRequest", StringComparison.Ordinal)),
                "RL-D3 dispatch-error evidence did not include MissingProfileRequest.");
        }

        Console.WriteLine("scenario RL-D3 passed");
    }
}