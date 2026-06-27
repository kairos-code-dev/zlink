using RuntimeMonitoring.Shared;
using Zlink.HttpClient;
using RuntimeMonitoring.Client.Support;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonC1DispatchFailureScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var trigger = ZLinkHttpClient.Create(options.TriggerUrl).Json().Build();
        using var throwService = ZLinkHttpClient.Create(options.ThrowServiceUrl).Json().Build();

        var failureReply = (await trigger.Post("/profile/request/throw")
            .Body(new ProfileRequest("throw", "mon-c1-request"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(failureReply.ProviderRid == "svc-throw", "MON-C1 direct trigger did not hit throwing-monitor service.");

        var (throwServiceEvidence, throwStderr) = await WaitForDispatchFailureEvidenceAsync(trigger, throwService);

        var recoveryReply = (await trigger.Post("/profile/request/throw")
            .Body(new ProfileRequest("throw", "mon-c1-recovery"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(recoveryReply.Value == "profile:throw", "MON-C1 messaging did not recover after monitoring handler failure.");

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

    static async Task<(string[] ThrowServiceEvidence, string[] ThrowStderr)> WaitForDispatchFailureEvidenceAsync(
        ZLinkHttpClient trigger,
        ZLinkHttpClient throwService)
    {
        var evidenceTask = throwService.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(
                [],
                [["monitor-socket|"], ["monitor-throw|"]],
                10000))
            .SubmitAsync<string[]>();
        var stderrTask = trigger.Post("/logs/throw-stderr/wait")
            .Body(new EvidenceWaitRequest(
                [],
                [["monitoring-event-dispatch"], ["monitoring dispatch failure for e2e"]],
                10000))
            .SubmitAsync<string[]>();

        var evidence = (await evidenceTask).Body;
        var stderr = (await stderrTask).Body;
        if (evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
            && evidence.Any(line => line.Contains("monitor-throw|", StringComparison.Ordinal))
            && stderr.Any(line => line.Contains("monitoring-event-dispatch", StringComparison.Ordinal))
            && stderr.Any(line => line.Contains("monitoring dispatch failure for e2e", StringComparison.Ordinal)))
        {
            return (evidence, stderr);
        }

        throw new InvalidOperationException("MON-C1 monitoring dispatch failure evidence was incomplete.");
    }
}
