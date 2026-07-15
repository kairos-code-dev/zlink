// Verifies MON-C1 Dispatch Failure behavior.
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonC1DispatchFailureScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var throwService = ZLinkHttpClient.Create(options.ThrowServiceUrl)
            .Timeout(TimeSpan.FromSeconds(30))
            .Build();
        var failureReply = (await throwService.Post("/profile/request")
            .Body(new ProfileReq("throw", "mon-c1-request"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(failureReply.ProviderRid == "svc-throw",
            "MON-C1 direct trigger did not hit throwing-monitor service.");

        var baseline = (await throwService.Get("/evidence").Async<string[]>()).Body.Length;
        await MonitoringProtocolTrigger.SendInvalidHandshakeAsync(options.ThrowChannelEndpoint);
        var throwServiceEvidence = await WaitForDispatchFailureEvidenceAsync(throwService, baseline);

        var recoveryReply = (await throwService.Post("/profile/request")
            .Body(new ProfileReq("throw", "mon-c1-recovery"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(recoveryReply.Value == "profile:throw",
            "MON-C1 messaging did not recover after monitoring handler failure.");

        ZlinkStreamAssert.Ensure(
            throwServiceEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)),
            "MON-C1 socket evidence missing.");
        ZlinkStreamAssert.Ensure(
            throwServiceEvidence.Any(line => line.Contains("monitor-throw|", StringComparison.Ordinal)),
            "MON-C1 throwing monitor evidence missing.");
        Console.WriteLine("scenario MON-C1 passed");
    }

    private static async Task<string[]> WaitForDispatchFailureEvidenceAsync(
        ZLinkHttpClient throwService,
        int afterIndex)
    {
        var evidenceTask = throwService.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                [],
                [["monitor-socket|"], ["monitor-throw|"]],
                AfterIndex: afterIndex))
            .Async<string[]>();
        var evidence = (await evidenceTask).Body;
        if (evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
            && evidence.Any(line => line.Contains("monitor-throw|", StringComparison.Ordinal)))
            return evidence;

        throw new InvalidOperationException("MON-C1 monitoring dispatch failure evidence was incomplete.");
    }
}
