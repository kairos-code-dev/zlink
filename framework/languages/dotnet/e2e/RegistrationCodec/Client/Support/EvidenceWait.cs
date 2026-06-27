using RegistrationCodec.Shared;
using Zlink.HttpClient;

namespace RegistrationCodec.Client;

internal static class EvidenceWait
{
    public static async Task<string[]> ForAllAsync(
        ZLinkHttpClient server,
        string[] containsAll,
        string failureMessage)
        => await ForAsync(
            server,
            new EvidenceWaitRequest(containsAll),
            evidence => containsAll.All(expected =>
                evidence.Any(line => line.Contains(expected, StringComparison.Ordinal))),
            failureMessage);

    public static async Task<string[]> ForAsync(
        ZLinkHttpClient server,
        EvidenceWaitRequest request,
        Func<string[], bool> predicate,
        string failureMessage)
    {
        var evidence = (await server.Post("/evidence/wait")
            .Body(request)
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(predicate(evidence), failureMessage);
        return evidence;
    }
}
