// Verifies that adding and removing a provider changes both topology and service-summary projections.
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA2RegistryEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var observer = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(35))
            .Build();
        var baseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;
        var service = await EphemeralService.StartAsync(options, "svc-c");
        var added = await WaitForProjectionAsync(observer, baseline, "added=svc-c");
        var addedSummary = AssertProjection(added, "added=svc-c", "MON-A2 add");
        ZlinkStreamAssert.Ensure(HasReadyServiceCount(addedSummary, 3),
            "MON-A2 add did not project three ready services.");

        var removedBaseline = baseline + added.Length;
        await service.DisposeAsync();
        var removed = await WaitForProjectionAsync(observer, removedBaseline, "removed=svc-c");
        var removedSummary = AssertProjection(removed, "removed=svc-c", "MON-A2 remove");
        ZlinkStreamAssert.Ensure(HasReadyServiceCount(removedSummary, 2),
            "MON-A2 remove did not restore the two-service projection.");
        Console.WriteLine("scenario MON-A2 passed");
    }

    private static async Task<string[]> WaitForProjectionAsync(
        ZLinkHttpClient observer,
        int afterIndex,
        string transition)
        => (await observer.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["kind=TopologyChanged", transition, "kind=ServiceSummaryChanged"],
                [],
                TimeoutMilliseconds: 30000,
                AfterIndex: afterIndex))
            .Async<string[]>()).Body;

    private static string AssertProjection(string[] evidence, string transition, string operation)
    {
        ZlinkStreamAssert.Ensure(evidence.Any(line =>
                line.Contains("kind=TopologyChanged", StringComparison.Ordinal)
                && line.Contains(transition, StringComparison.Ordinal)
                && line.Contains("entries=", StringComparison.Ordinal)),
            $"{operation} topology projection did not contain the actual transition.");
        ZlinkStreamAssert.Ensure(evidence.Any(line =>
                line.Contains("kind=ServiceSummaryChanged", StringComparison.Ordinal)
                && line.Contains("summary-entries=", StringComparison.Ordinal)
                && !line.EndsWith("summary-entries=", StringComparison.Ordinal)),
            $"{operation} service-summary projection evidence missing.");
        return LatestSummary(evidence);
    }

    private static string LatestSummary(IEnumerable<string> evidence)
        => evidence.LastOrDefault(line => line.Contains("kind=ServiceSummaryChanged", StringComparison.Ordinal))
           ?? throw new InvalidOperationException("Location service-summary evidence is missing.");

    private static bool HasReadyServiceCount(string summary, int count)
        => summary.Contains($":{count}:{count}:0:0", StringComparison.Ordinal);
}
