using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonC1DispatchFailureScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var throwService = ZLinkHttpClient.Create(options.ThrowServiceUrl).Build();
        await using var trigger = await MonitoringChannelClient.StartAsync(
            options, options.ThrowChannelEndpoint, "trigger-mon-c1");
        var failureReply = await trigger.RequestAsync(new ProfileReq("throw", "mon-c1-request"));
        ScenarioAssert.That(failureReply.ProviderRid == "svc-throw",
            "MON-C1 direct trigger did not hit throwing-monitor service.");

        var throwServiceEvidence = await WaitForDispatchFailureEvidenceAsync(throwService);

        var recoveryReply = await trigger.RequestAsync(new ProfileReq("throw", "mon-c1-recovery"));
        ScenarioAssert.That(recoveryReply.Value == "profile:throw",
            "MON-C1 messaging did not recover after monitoring handler failure.");

        ScenarioAssert.That(
            throwServiceEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)),
            "MON-C1 socket evidence missing.");
        ScenarioAssert.That(
            throwServiceEvidence.Any(line => line.Contains("monitor-throw|", StringComparison.Ordinal)),
            "MON-C1 throwing monitor evidence missing.");
        Console.WriteLine("scenario MON-C1 passed");
    }

    private static async Task<string[]> WaitForDispatchFailureEvidenceAsync(ZLinkHttpClient throwService)
    {
        var evidenceTask = throwService.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                [],
                [["monitor-socket|"], ["monitor-throw|"]]))
            .SubmitAsync<string[]>();
        var evidence = (await evidenceTask).Body;
        if (evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
            && evidence.Any(line => line.Contains("monitor-throw|", StringComparison.Ordinal)))
            return evidence;

        throw new InvalidOperationException("MON-C1 monitoring dispatch failure evidence was incomplete.");
    }
}
