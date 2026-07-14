using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

// MON-A2 verifies the location-runtime source: the observer node's own
// projection (peer rows + connection state) emits TopologyChanged /
// ServiceSummaryChanged with real content as the deployment converges.
internal static class MonA2RegistryEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var service = ZLinkHttpClient.Create(options.ServiceUrl).Build();
        var evidence = (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["monitor-location-runtime|source=location-runtime"],
                [["kind=TopologyChanged"], ["kind=ServiceSummaryChanged"]]))
            .Async<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line =>
                line.Contains("monitor-location-runtime|source=location-runtime|kind=TopologyChanged", StringComparison.Ordinal)
                && !line.Contains("topology=0", StringComparison.Ordinal)
                && !line.Contains("topology=-1", StringComparison.Ordinal)),
            "MON-A2 topology evidence missing.");
        ScenarioAssert.That(
            evidence.Any(line =>
                line.Contains("monitor-location-runtime|source=location-runtime|kind=ServiceSummaryChanged", StringComparison.Ordinal)
                && !line.Contains("summary=0", StringComparison.Ordinal)
                && !line.Contains("summary=-1", StringComparison.Ordinal)),
            "MON-A2 service summary evidence missing.");
        Console.WriteLine("scenario MON-A2 passed");
    }
}
