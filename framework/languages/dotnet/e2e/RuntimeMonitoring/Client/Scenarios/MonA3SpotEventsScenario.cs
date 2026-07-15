using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA3SpotEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var service = ZLinkHttpClient.Create(options.ServiceUrl).Build();
        var before = (await service.Get("/evidence").Async<string[]>()).Body;
        await service.Post("/admin/subject/create").AsyncRaw();
        var evidence = await WaitForSpotEvidenceAsync(service, before);
        await service.Post("/admin/subject/close").AsyncRaw();
        ZlinkStreamAssert.Ensure(
            evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=StatusChanged", StringComparison.Ordinal)),
            "MON-A3 spot status evidence missing.");
        ZlinkStreamAssert.Ensure(
            evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=PeersChanged", StringComparison.Ordinal)),
            "MON-A3 spot peer evidence missing.");
        ZlinkStreamAssert.Ensure(
            evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=SubjectsChanged", StringComparison.Ordinal)
                && line.Contains("monitor.dynamic", StringComparison.Ordinal)),
            "MON-A3 spot subject evidence missing.");
        ZlinkStreamAssert.Ensure(
            evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=TimerHandlerFailed", StringComparison.Ordinal)
                && line.Contains("timer=failing", StringComparison.Ordinal)),
            "MON-A3 spot timer failure evidence missing.");
        Console.WriteLine("scenario MON-A3 passed");
    }

    private static async Task<string[]> WaitForSpotEvidenceAsync(
        ZLinkHttpClient service,
        string[] before)
    {
        var evidence = (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["monitor-spot|source=monitor.spot"],
                [
                    ["kind=SubjectsChanged"],
                    ["monitor.dynamic"]
                ],
                AfterIndex: before.Length))
            .Async<string[]>()).Body;
        evidence = before.Concat(evidence).ToArray();
        if (evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=StatusChanged", StringComparison.Ordinal))
            && evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=PeersChanged", StringComparison.Ordinal))
            && evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=SubjectsChanged", StringComparison.Ordinal)
                && line.Contains("monitor.dynamic", StringComparison.Ordinal))
            && evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=TimerHandlerFailed", StringComparison.Ordinal)
                && line.Contains("timer=failing", StringComparison.Ordinal)))
            return evidence;

        throw new InvalidOperationException("MON-A3 spot status/peer/subject/timer evidence was incomplete.");
    }
}
