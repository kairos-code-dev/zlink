using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA4AvailabilityTransitionScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var service = ZLinkHttpClient.Create(options.ServiceUrl).Build();
        await using var trigger = await MonitoringChannelClient.StartAsync(
            options, options.ServiceChannelEndpoint, "trigger-mon-a4", monitorSocketEvents: true);
        var before = await trigger.RequestAsync(new ProfileReq("drain", "mon-a4-before-drain"));
        ScenarioAssert.That(before.ProviderRid == "svc-a", "MON-A4 direct trigger did not hit drained service.");

        await service.Post("/admin/drain").AsyncRaw();
        var triggerEvidence = await WaitForTriggerDrainEvidenceAsync(trigger);
        await service.Post("/admin/restore").AsyncRaw();

        ScenarioAssert.That(
            triggerEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                        && line.Contains("kind=PeerAdmissionChanged", StringComparison.Ordinal)),
            "MON-A4 trigger socket drain transition evidence missing.");

        var serviceEvidence = await WaitForServiceDrainEvidenceAsync(service);
        ScenarioAssert.That(
            serviceEvidence.Any(line => line.Contains("admin|", StringComparison.Ordinal)
                                        && line.Contains("action=drain", StringComparison.Ordinal)),
            "MON-A4 service drain evidence missing.");

        var topologyEvidence = await WaitForLocationTopologyEvidenceAsync(service);
        ScenarioAssert.That(
            topologyEvidence.Any(line =>
                line.Contains(
                    "monitor-location-runtime|source=location-runtime|kind=TopologyChanged",
                    StringComparison.Ordinal)),
            "MON-A4 location runtime topology evidence missing.");
        Console.WriteLine("scenario MON-A4 passed");
    }

    private static async Task<string[]> WaitForTriggerDrainEvidenceAsync(MonitoringChannelClient trigger)
    {
        var evidence = await trigger.WaitForEvidenceAsync(entries => entries.Any(line =>
            line.Contains("monitor-socket|", StringComparison.Ordinal)
            && line.Contains("kind=PeerAdmissionChanged", StringComparison.Ordinal)));
        if (evidence.Any(line => line.Contains("kind=PeerAdmissionChanged", StringComparison.Ordinal))) return evidence;

        throw new InvalidOperationException("MON-A4 drain transition evidence was incomplete.");
    }

    private static async Task<string[]> WaitForLocationTopologyEvidenceAsync(ZLinkHttpClient service)
    {
        return (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["monitor-location-runtime|source=location-runtime"],
                [["kind=TopologyChanged"]]))
            .Async<string[]>()).Body;
    }

    private static async Task<string[]> WaitForServiceDrainEvidenceAsync(ZLinkHttpClient service)
    {
        var evidence = (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["admin|"],
                [["action=drain"]]))
            .Async<string[]>()).Body;
        if (evidence.Any(line => line.Contains("admin|", StringComparison.Ordinal)
                                 && line.Contains("action=drain", StringComparison.Ordinal)))
            return evidence;

        throw new InvalidOperationException("MON-A4 service drain evidence was incomplete.");
    }
}
