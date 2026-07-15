using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA4AvailabilityTransitionScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var serviceA = ZLinkHttpClient.Create(options.ServiceUrl).Build();
        using var serviceB = ZLinkHttpClient.Create(options.ServiceBUrl).Build();

        await using var caller = await MonitoringChannelClient.StartAutoConnectedAsync(
            options,
            "trigger-mon-a4");

        await serviceB.Post("/admin/drain").AsyncRaw();
        await caller.WaitForEvidenceAsync(entries =>
            HasAdmission(entries, 0, options.ServiceBChannelEndpoint, 0));
        var before = await caller.RequestAsync(new ProfileReq("before", "mon-a4-before"));
        ScenarioAssert.That(before.ProviderRid == "svc-a", "MON-A4 initial request did not use svc-a.");

        var failoverBaseline = caller.GetEvidence().Length;
        await serviceB.Post("/admin/restore").AsyncRaw();
        await serviceA.Post("/admin/drain").AsyncRaw();
        await caller.WaitForEvidenceAsync(entries =>
            HasAdmission(entries, failoverBaseline, options.ServiceBChannelEndpoint, 100)
            && HasAdmission(entries, failoverBaseline, options.ServiceChannelEndpoint, 0));

        // This is deliberately the first request after the admission transition: no retry
        // loop or delay is allowed to hide a failed failover.
        var failedOver = await caller.RequestAsync(new ProfileReq("after", "mon-a4-after"));
        ScenarioAssert.That(failedOver.ProviderRid == "svc-b",
            "MON-A4 request did not fail over to svc-b. evidence="
            + string.Join(";", caller.GetEvidence().Skip(failoverBaseline)));

        var restoreBaseline = caller.GetEvidence().Length;
        await serviceA.Post("/admin/restore").AsyncRaw();
        var evidence = await caller.WaitForEvidenceAsync(entries =>
            HasAdmission(entries, restoreBaseline, options.ServiceChannelEndpoint, 100));
        ScenarioAssert.That(HasAdmission(
                evidence, restoreBaseline, options.ServiceChannelEndpoint, 100),
            "MON-A4 restore admission transition was not observed.");
        Console.WriteLine("scenario MON-A4 passed");
    }

    private static bool HasAdmission(
        IEnumerable<string> evidence,
        int afterIndex,
        string endpoint,
        uint value) => evidence.Skip(afterIndex).Any(line =>
        line.Contains("kind=PeerAdmissionChanged", StringComparison.Ordinal)
        && line.Contains($"remote={endpoint}", StringComparison.Ordinal)
        && line.Contains($"value={value}", StringComparison.Ordinal));

}
