using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA5FixedKindsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var service = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(30))
            .Build();
        await MonitoringChannelClient.SendInvalidHandshakeAsync(options.ServiceChannelEndpoint);
        var serviceEvidence = await WaitForFixedKindsAsync(service);

        ScenarioAssert.That(
                serviceEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                        && line.Contains("kind=HandshakeFailed", StringComparison.Ordinal)),
            "MON-A5 handshake failure evidence missing.");
        ScenarioAssert.That(
            serviceEvidence.Any(line =>
                line.Contains(
                    "monitor-location-runtime|source=location-runtime|kind=StatusChanged",
                    StringComparison.Ordinal)),
            "MON-A5 location runtime status evidence missing.");
        ScenarioAssert.That(
            serviceEvidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=StatusChanged", StringComparison.Ordinal)),
            "MON-A5 spot status evidence missing.");
        ScenarioAssert.That(
            serviceEvidence.Any(line =>
                line.Contains("monitor-spot|source=monitor.spot|kind=TimerStoppedAfterUnhandledException",
                    StringComparison.Ordinal)
                && line.Contains("timer=stopping", StringComparison.Ordinal)),
            "MON-A5 stopped timer evidence missing.");
        Console.WriteLine("scenario MON-A5 passed");
    }

    private static async Task<string[]> WaitForFixedKindsAsync(ZLinkHttpClient service)
    {
        var evidence = (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                [],
                [
                    ["kind=HandshakeFailed"],
                    ["monitor-location-runtime|source=location-runtime|kind=StatusChanged"],
                    ["monitor-spot|source=monitor.spot|kind=StatusChanged"],
                    ["monitor-spot|source=monitor.spot|kind=TimerStoppedAfterUnhandledException"],
                    ["timer=stopping"]
                ]))
            .Async<string[]>()).Body;
        if (HasFixedKinds(evidence)) return evidence;

        throw new InvalidOperationException("MON-A5 fixed monitoring evidence was incomplete.");
    }

    private static bool HasFixedKinds(string[] evidence)
    {
        return evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                    && line.Contains("kind=HandshakeFailed", StringComparison.Ordinal))
               && evidence.Any(line => line.Contains(
                   "monitor-location-runtime|source=location-runtime|kind=StatusChanged",
                   StringComparison.Ordinal))
               && evidence.Any(line => line.Contains(
                   "monitor-spot|source=monitor.spot|kind=StatusChanged", StringComparison.Ordinal))
               && evidence.Any(line => line.Contains(
                       "monitor-spot|source=monitor.spot|kind=TimerStoppedAfterUnhandledException",
                       StringComparison.Ordinal)
                   && line.Contains("timer=stopping", StringComparison.Ordinal));
    }
}
