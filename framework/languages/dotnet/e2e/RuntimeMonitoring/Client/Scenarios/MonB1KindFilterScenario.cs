using RuntimeMonitoring.Shared;
using Zlink.HttpClient;
using RuntimeMonitoring.Client.Support;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonB1KindFilterScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var trigger = ZLinkHttpClient.Create(options.TriggerUrl).Json().Build();
        using var serviceB = ZLinkHttpClient.Create(options.ServiceBUrl).Json().Build();

        var reply = (await trigger.Post("/profile/request/service-b")
            .Body(new ProfileRequest("filter", "mon-b1-request"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.ProviderRid == "svc-b", "MON-B1 direct trigger did not hit filtered service.");

        var serviceBEvidence = await WaitForFilteredSocketEvidenceAsync(serviceB);
        ScenarioAssert.That(
            serviceBEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                && line.Contains("kind=ConnectionReady", StringComparison.Ordinal)),
            "MON-B1 filtered socket evidence missing.");
        ScenarioAssert.That(
            serviceBEvidence
                .Where(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
                .All(line => line.Contains("kind=ConnectionReady", StringComparison.Ordinal)),
            "MON-B1 returned a socket event outside the configured kind filter.");
        Console.WriteLine("scenario MON-B1 passed");
    }

    static async Task<string[]> WaitForFilteredSocketEvidenceAsync(ZLinkHttpClient serviceB)
    {
        var evidence = (await serviceB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(
                ["monitor-socket|"],
                [["kind=ConnectionReady"]],
                10000))
            .SubmitAsync<string[]>()).Body;
        if (evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                && line.Contains("kind=ConnectionReady", StringComparison.Ordinal))
            && evidence.Where(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
                .All(line => line.Contains("kind=ConnectionReady", StringComparison.Ordinal)))
        {
            return evidence;
        }

        throw new InvalidOperationException("MON-B1 socket event kind filter evidence was incomplete.");
    }
}
