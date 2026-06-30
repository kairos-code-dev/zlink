using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonC1DispatchFailureScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var trigger = ZLinkHttpClient.Create(options.TriggerUrl).Build();
        using var throwService = ZLinkHttpClient.Create(options.ThrowServiceUrl).Build();

        var failureReply = (await trigger.Post("/profile/request/throw")
            .Body(new ProfileReq("throw", "mon-c1-request"))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(failureReply.ProviderRid == "svc-throw",
            "MON-C1 direct trigger did not hit throwing-monitor service.");

        var (throwServiceEvidence, throwStderr) = await WaitForDispatchFailureEvidenceAsync(trigger, throwService);

        var recoveryReply = (await trigger.Post("/profile/request/throw")
            .Body(new ProfileReq("throw", "mon-c1-recovery"))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(recoveryReply.Value == "profile:throw",
            "MON-C1 messaging did not recover after monitoring handler failure.");

        ScenarioAssert.That(
            throwServiceEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)),
            "MON-C1 socket evidence missing.");
        ScenarioAssert.That(
            throwServiceEvidence.Any(line => line.Contains("monitor-throw|", StringComparison.Ordinal)),
            "MON-C1 throwing monitor evidence missing.");
        ScenarioAssert.That(
            throwStderr.Any(line => line.Contains("monitoring-event-dispatch", StringComparison.Ordinal))
            && throwStderr.Any(line => line.Contains("monitoring dispatch failure for e2e", StringComparison.Ordinal)),
            "MON-C1 dispatch failure log evidence missing.");
        Console.WriteLine("scenario MON-C1 passed");
    }

    private static async Task<(string[] ThrowServiceEvidence, string[] ThrowStderr)>
        WaitForDispatchFailureEvidenceAsync(
            ZLinkHttpClient trigger,
            ZLinkHttpClient throwService)
    {
        var evidenceTask = throwService.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                [],
                [["monitor-socket|"], ["monitor-throw|"]]))
            .SubmitAsync<string[]>();
        var stderrTask = trigger.Post("/logs/throw-stderr/wait")
            .Body(new EvidenceWaitReq(
                [],
                [["monitoring-event-dispatch"], ["monitoring dispatch failure for e2e"]]))
            .SubmitAsync<string[]>();

        var evidence = (await evidenceTask).Body;
        var stderr = (await stderrTask).Body;
        if (evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
            && evidence.Any(line => line.Contains("monitor-throw|", StringComparison.Ordinal))
            && stderr.Any(line => line.Contains("monitoring-event-dispatch", StringComparison.Ordinal))
            && stderr.Any(line => line.Contains("monitoring dispatch failure for e2e", StringComparison.Ordinal)))
            return (evidence, stderr);

        throw new InvalidOperationException("MON-C1 monitoring dispatch failure evidence was incomplete.");
    }
}