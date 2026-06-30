using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA5FixedKindsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var trigger = ZLinkHttpClient.Create(options.TriggerUrl).Json().Build();
        using var service = ZLinkHttpClient.Create(options.ServiceUrl).Json().Build();
        using var registry = ZLinkHttpClient.Create(options.RegistryUrl).Json().Build();

        await trigger.Post("/socket/handshake-failure").SubmitRawAsync();
        var (serviceEvidence, registryEvidence) = await WaitForFixedKindsAsync(service, registry);

        ScenarioAssert.That(
            serviceEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                        && (line.Contains("kind=HandshakeFailed", StringComparison.Ordinal)
                                            || line.Contains("kind=Internal", StringComparison.Ordinal))),
            "MON-A5 handshake failure evidence missing.");
        ScenarioAssert.That(
            registryEvidence.Any(line =>
                line.Contains("monitor-registry|source=registry|kind=StatusChanged", StringComparison.Ordinal)),
            "MON-A5 registry status evidence missing.");
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

    private static async Task<(string[] ServiceEvidence, string[] RegistryEvidence)> WaitForFixedKindsAsync(
        ZLinkHttpClient service,
        ZLinkHttpClient registry)
    {
        var serviceEvidenceTask = service.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(
                [],
                [
                    ["kind=HandshakeFailed", "kind=Internal"],
                    ["monitor-spot|source=monitor.spot|kind=StatusChanged"],
                    ["monitor-spot|source=monitor.spot|kind=TimerStoppedAfterUnhandledException"],
                    ["timer=stopping"]
                ]))
            .SubmitAsync<string[]>();
        var registryEvidenceTask = registry.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(
                ["monitor-registry|source=registry"],
                [["kind=StatusChanged"]]))
            .SubmitAsync<string[]>();

        var serviceEvidence = (await serviceEvidenceTask).Body;
        var registryEvidence = (await registryEvidenceTask).Body;
        if (HasFixedKinds(serviceEvidence, registryEvidence)) return (serviceEvidence, registryEvidence);

        throw new InvalidOperationException("MON-A5 fixed monitoring evidence was incomplete.");
    }

    private static bool HasFixedKinds(string[] serviceEvidence, string[] registryEvidence)
    {
        return serviceEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                           && (line.Contains("kind=HandshakeFailed", StringComparison.Ordinal)
                                               || line.Contains("kind=Internal", StringComparison.Ordinal)))
               && registryEvidence.Any(line =>
                   line.Contains("monitor-registry|source=registry|kind=StatusChanged", StringComparison.Ordinal))
               && serviceEvidence.Any(line =>
                   line.Contains("monitor-spot|source=monitor.spot|kind=StatusChanged", StringComparison.Ordinal))
               && serviceEvidence.Any(line =>
                   line.Contains("monitor-spot|source=monitor.spot|kind=TimerStoppedAfterUnhandledException",
                       StringComparison.Ordinal)
                   && line.Contains("timer=stopping", StringComparison.Ordinal));
    }
}