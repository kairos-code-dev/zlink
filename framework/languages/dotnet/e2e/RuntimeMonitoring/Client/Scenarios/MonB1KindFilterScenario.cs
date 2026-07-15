// Verifies MON-B1 Kind Filter behavior.
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonB1KindFilterScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var serviceB = ZLinkHttpClient.Create(options.FilteredServiceUrl)
            .Timeout(TimeSpan.FromSeconds(30))
            .Build();
        var reply = (await serviceB.Post("/profile/request")
            .Body(new ProfileReq("filter", "mon-b1-request"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(reply.ProviderRid == "svc-filtered", "MON-B1 direct trigger did not hit filtered service.");
        var baseline = (await serviceB.Get("/evidence").Async<string[]>()).Body.Length;
        await serviceB.Post("/admin/disconnect").AsyncRaw();
        await serviceB.Post("/admin/connect").AsyncRaw();
        var serviceBEvidence = await WaitForFilteredSocketEvidenceAsync(serviceB, baseline);

        ZlinkStreamAssert.Ensure(
            serviceBEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                         && line.Contains("kind=ConnectionReady", StringComparison.Ordinal)),
            "MON-B1 filtered socket evidence missing.");
        ZlinkStreamAssert.Ensure(
            serviceBEvidence
                .Where(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
                .All(line => line.Contains("kind=ConnectionReady", StringComparison.Ordinal)),
            "MON-B1 returned a socket event outside the configured kind filter.");
        Console.WriteLine("scenario MON-B1 passed");
    }

    private static async Task<string[]> WaitForFilteredSocketEvidenceAsync(
        ZLinkHttpClient serviceB,
        int afterIndex)
    {
        var evidence = (await serviceB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["monitor-socket|"],
                [["kind=ConnectionReady"]],
                AfterIndex: afterIndex))
            .Async<string[]>()).Body;
        if (evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                 && line.Contains("kind=ConnectionReady", StringComparison.Ordinal))
            && evidence.Where(line => line.Contains("monitor-socket|", StringComparison.Ordinal))
                .All(line => line.Contains("kind=ConnectionReady", StringComparison.Ordinal)))
            return evidence;

        throw new InvalidOperationException("MON-B1 socket event kind filter evidence was incomplete.");
    }
}
