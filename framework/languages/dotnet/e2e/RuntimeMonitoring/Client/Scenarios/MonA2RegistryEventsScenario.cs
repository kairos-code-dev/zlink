using RuntimeMonitoring.Shared;
using Zlink.HttpClient;
using RuntimeMonitoring.Client.Support;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA2RegistryEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var registry = ZLinkHttpClient.Create(options.RegistryUrl).Json().Build();
        var evidence = await WaitForRegistryEvidenceAsync(registry);
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("monitor-registry|source=registry|kind=TopologyChanged", StringComparison.Ordinal)
                && !line.Contains("topology=0", StringComparison.Ordinal)),
            "MON-A2 topology evidence missing.");
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("monitor-registry|source=registry|kind=ServiceSummaryChanged", StringComparison.Ordinal)
                && !line.Contains("summary=0", StringComparison.Ordinal)),
            "MON-A2 service summary evidence missing.");
        Console.WriteLine("scenario MON-A2 passed");
    }

    static async Task<string[]> WaitForRegistryEvidenceAsync(ZLinkHttpClient registry)
    {
        var evidence = (await registry.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(
                ["monitor-registry|source=registry"],
                [["kind=TopologyChanged|topology=3"], ["kind=ServiceSummaryChanged|topology=-1|summary=3"]],
                10000))
            .SubmitAsync<string[]>()).Body;
        if (evidence.Any(line => line.Contains("monitor-registry|source=registry|kind=TopologyChanged", StringComparison.Ordinal)
                && !line.Contains("topology=0", StringComparison.Ordinal))
            && evidence.Any(line => line.Contains("monitor-registry|source=registry|kind=ServiceSummaryChanged", StringComparison.Ordinal)
                && !line.Contains("summary=0", StringComparison.Ordinal)))
        {
            return evidence;
        }

        throw new InvalidOperationException("MON-A2 registry topology/summary evidence was incomplete.");
    }
}
