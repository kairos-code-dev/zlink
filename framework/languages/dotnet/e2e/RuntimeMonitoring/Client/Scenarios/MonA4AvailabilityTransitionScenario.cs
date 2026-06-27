using RuntimeMonitoring.Shared;
using Zlink.HttpClient;
using RuntimeMonitoring.Client.Support;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA4AvailabilityTransitionScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var trigger = ZLinkHttpClient.Create(options.TriggerUrl).Json().Build();
        using var service = ZLinkHttpClient.Create(options.ServiceUrl).Json().Build();
        using var registry = ZLinkHttpClient.Create(options.RegistryUrl).Json().Build();

        var before = (await trigger.Post("/profile/request")
            .Body(new ProfileRequest("drain", "mon-a4-before-drain"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(before.ProviderRid == "svc-a", "MON-A4 direct trigger did not hit drained service.");

        await service.Post("/admin/drain").SubmitRawAsync();
        var triggerEvidence = await WaitForTriggerDrainEvidenceAsync(trigger);
        await service.Post("/admin/restore").SubmitRawAsync();

        ScenarioAssert.That(
            triggerEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                && line.Contains("kind=PeerAdmissionChanged", StringComparison.Ordinal)),
            "MON-A4 trigger socket drain transition evidence missing.");

        var serviceEvidence = await WaitForServiceDrainEvidenceAsync(service);
        ScenarioAssert.That(
            serviceEvidence.Any(line => line.Contains("admin|", StringComparison.Ordinal)
                && line.Contains("action=drain", StringComparison.Ordinal)),
            "MON-A4 service drain evidence missing.");

        var registryEvidence = await WaitForRegistryTopologyEvidenceAsync(registry);
        ScenarioAssert.That(
            registryEvidence.Count(line => line.Contains("monitor-registry|source=registry|kind=TopologyChanged", StringComparison.Ordinal)) >= 2,
            "MON-A4 registry topology transition evidence missing.");
        Console.WriteLine("scenario MON-A4 passed");
    }

    static async Task<string[]> WaitForTriggerDrainEvidenceAsync(ZLinkHttpClient trigger)
    {
        var evidence = (await trigger.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(
                ["monitor-socket|"],
                [["kind=PeerAdmissionChanged"]],
                10000))
            .SubmitAsync<string[]>()).Body;
        if (evidence.Any(line => line.Contains("kind=PeerAdmissionChanged", StringComparison.Ordinal)))
        {
            return evidence;
        }

        throw new InvalidOperationException("MON-A4 drain transition evidence was incomplete.");
    }

    static async Task<string[]> WaitForRegistryTopologyEvidenceAsync(ZLinkHttpClient registry)
    {
        var evidence = (await registry.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(
                ["monitor-registry|source=registry"],
                [["kind=TopologyChanged|topology=3"]],
                10000))
            .SubmitAsync<string[]>()).Body;
        if (evidence.Count(line => line.Contains("monitor-registry|source=registry|kind=TopologyChanged", StringComparison.Ordinal)) >= 2)
        {
            return evidence;
        }

        throw new InvalidOperationException("MON-A4 registry topology transition evidence was incomplete.");
    }

    static async Task<string[]> WaitForServiceDrainEvidenceAsync(ZLinkHttpClient service)
    {
        var evidence = (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(
                ["admin|"],
                [["action=drain"]],
                10000))
            .SubmitAsync<string[]>()).Body;
        if (evidence.Any(line => line.Contains("admin|", StringComparison.Ordinal)
            && line.Contains("action=drain", StringComparison.Ordinal)))
        {
            return evidence;
        }

        throw new InvalidOperationException("MON-A4 service drain evidence was incomplete.");
    }
}
