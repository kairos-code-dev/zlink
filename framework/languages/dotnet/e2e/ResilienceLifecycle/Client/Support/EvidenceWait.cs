using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client;

internal static class EvidenceWait
{
    public static async Task<string[]> AnyProviderAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        string contains,
        string failureMessage)
    {
        return await AnyProviderAsync(
            providerA,
            providerB,
            new EvidenceWaitRequest([contains], []),
            line => line.Contains(contains, StringComparison.Ordinal),
            failureMessage);
    }

    public static async Task<string[]> AnyProviderAsync(
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB,
        EvidenceWaitRequest request,
        Func<string, bool> predicate,
        string failureMessage)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        var waitA = providerA.Post("/evidence/wait").Body(request).SubmitAsync<string[]>(timeout.Token).AsTask();
        var waitB = providerB.Post("/evidence/wait").Body(request).SubmitAsync<string[]>(timeout.Token).AsTask();
        var completed = await Task.WhenAny(waitA, waitB);
        var evidence = (await completed).Body;
        timeout.Cancel();
        ScenarioAssert.That(evidence.Any(predicate), failureMessage);
        return evidence;
    }
}
