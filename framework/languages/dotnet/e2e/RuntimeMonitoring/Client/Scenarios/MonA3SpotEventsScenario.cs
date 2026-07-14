using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA3SpotEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var service = ZLinkHttpClient.Create(options.ServiceUrl).Build();
        var evidence = await WaitForSpotEvidenceAsync(service);
        ScenarioAssert.That(
            evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=StatusChanged", StringComparison.Ordinal)),
            "MON-A3 spot status evidence missing.");
        ScenarioAssert.That(
            evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=PeersChanged", StringComparison.Ordinal)),
            "MON-A3 spot peer evidence missing.");
        ScenarioAssert.That(
            evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=SubjectsChanged", StringComparison.Ordinal)),
            "MON-A3 spot subject evidence missing.");
        ScenarioAssert.That(
            evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=TimerHandlerFailed", StringComparison.Ordinal)
                && line.Contains("timer=failing", StringComparison.Ordinal)),
            "MON-A3 spot timer failure evidence missing.");
        Console.WriteLine("scenario MON-A3 passed");
    }

    private static async Task<string[]> WaitForSpotEvidenceAsync(ZLinkHttpClient service)
    {
        var evidence = (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["monitor-spot|source=monitor.spot"],
                [
                    ["kind=StatusChanged"],
                    ["kind=PeersChanged"],
                    ["kind=SubjectsChanged"],
                    ["kind=TimerHandlerFailed"],
                    ["timer=failing"]
                ]))
            .Async<string[]>()).Body;
        if (evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=StatusChanged", StringComparison.Ordinal))
            && evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=PeersChanged", StringComparison.Ordinal))
            && evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=SubjectsChanged", StringComparison.Ordinal))
            && evidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=TimerHandlerFailed", StringComparison.Ordinal)
                && line.Contains("timer=failing", StringComparison.Ordinal)))
            return evidence;

        throw new InvalidOperationException("MON-A3 spot status/peer/subject/timer evidence was incomplete.");
    }
}