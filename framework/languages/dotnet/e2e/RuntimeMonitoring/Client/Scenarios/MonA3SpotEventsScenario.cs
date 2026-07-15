// Verifies peer, subject, and timer-failure spot events caused after the scenario baseline.
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA3SpotEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var service = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(30))
            .Build();
        var baseline = (await service.Get("/evidence").Async<string[]>()).Body.Length;
        var peer = await EphemeralService.StartAsync(options, "svc-spot");
        try
        {
            await service.Post("/admin/subject/create").AsyncRaw();
            var evidence = (await service.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    ["monitor-spot|source=monitor.spot", "kind=PeersChanged", "kind=SubjectsChanged",
                        "monitor.dynamic", "kind=TimerHandlerFailed", "timer=failing"],
                    [],
                    TimeoutMilliseconds: 30000,
                    AfterIndex: baseline))
                .Async<string[]>()).Body;
            ZlinkStreamAssert.Ensure(evidence.Any(line =>
                    line.Contains("kind=PeersChanged", StringComparison.Ordinal)
                    && !line.Contains("peers=-1", StringComparison.Ordinal)),
                "MON-A3 spot peer change payload missing.");
            ZlinkStreamAssert.Ensure(evidence.Any(line =>
                    line.Contains("kind=SubjectsChanged", StringComparison.Ordinal)
                    && line.Contains("monitor.dynamic", StringComparison.Ordinal)),
                "MON-A3 spot subject change payload missing.");
            ZlinkStreamAssert.Ensure(evidence.Any(line =>
                    line.Contains("kind=TimerHandlerFailed", StringComparison.Ordinal)
                    && line.Contains("timer=failing", StringComparison.Ordinal)),
                "MON-A3 spot timer failure payload missing.");
            await service.Post("/admin/subject/close").AsyncRaw();
        }
        finally
        {
            await peer.DisposeAsync();
        }
        Console.WriteLine("scenario MON-A3 passed");
    }
}
