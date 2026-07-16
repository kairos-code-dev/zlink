// Verifies that provider drain and restore are observable before the first request uses the new provider.
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA4AvailabilityTransitionScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var serviceA = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(30))
            .Build();
        using var serviceB = ZLinkHttpClient.Create(options.ServiceBUrl).Build();

        await VerifySameRidEndpointFailoverAsync(options, serviceA);

        await serviceB.Post("/admin/weight/exclude").AsyncRaw();
        var drainedB = await WaitForEvidenceAsync(
            serviceA,
            ["source=monitor.profile.client", "kind=PeerAdmissionChanged", options.ServiceBChannelEndpoint, "value=0"]);
        ZlinkStreamAssert.Ensure(
            HasAdmission(drainedB, options.ServiceBChannelEndpoint, 0),
            "MON-A4 did not observe svc-b drain admission.");

        var before = await RequestAsync(serviceA, new ProfileReq("before", "mon-a4-before"));
        ZlinkStreamAssert.Ensure(before.ProviderRid == "svc-a", "MON-A4 initial request did not use svc-a.");

        var failoverBaseline = (await serviceA.Get("/evidence").Async<string[]>()).Body.Length;
        await serviceB.Post("/admin/weight/include").AsyncRaw();
        await serviceA.Post("/admin/weight/exclude").AsyncRaw();
        var failedOverEvidence = await WaitForEvidenceAsync(
            serviceA,
            ["source=monitor.profile.client", "kind=PeerAdmissionChanged", options.ServiceBChannelEndpoint,
                "value=100", options.ServiceChannelEndpoint, "value=0"],
            failoverBaseline);
        ZlinkStreamAssert.Ensure(
            HasAdmission(failedOverEvidence, options.ServiceBChannelEndpoint, 100)
            && HasAdmission(failedOverEvidence, options.ServiceChannelEndpoint, 0),
            "MON-A4 failover admission transitions were incomplete.");

        // This is the first request after the observed transition; no retry or delay may hide a failover defect.
        var failedOver = await RequestAsync(serviceA, new ProfileReq("after", "mon-a4-after"));
        ZlinkStreamAssert.Ensure(failedOver.ProviderRid == "svc-b",
            "MON-A4 request did not fail over to svc-b. evidence=" + string.Join(";", failedOverEvidence));

        var restoreBaseline = (await serviceA.Get("/evidence").Async<string[]>()).Body.Length;
        await serviceA.Post("/admin/weight/include").AsyncRaw();
        var restored = await WaitForEvidenceAsync(
            serviceA,
            ["source=monitor.profile.client", "kind=PeerAdmissionChanged", options.ServiceChannelEndpoint, "value=100"],
            restoreBaseline);
        ZlinkStreamAssert.Ensure(HasAdmission(restored, options.ServiceChannelEndpoint, 100),
            "MON-A4 restore admission transition was not observed.");
        Console.WriteLine("scenario MON-A4 passed");
    }

    private static async Task VerifySameRidEndpointFailoverAsync(
        ClientOptions options,
        ZLinkHttpClient observer)
    {
        const string failoverRid = "svc-a4-failover";
        var addBaseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;
        var first = await EphemeralService.StartAsync(options, failoverRid);
        var firstAdded = await WaitForEvidenceAsync(
            observer,
            ["kind=TopologyChanged", $"added={failoverRid}", "kind=ServiceSummaryChanged",
                "source=monitor.profile.client", first.ChannelEndpoint],
            addBaseline);
        ZlinkStreamAssert.Ensure(
            HasSocketTransition(firstAdded, first.ChannelEndpoint, "Connected", "ConnectionReady"),
            "MON-A4 first failover endpoint did not produce a connection transition.");

        var removeBaseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;
        var drain = await first.DrainAsync();
        ZlinkStreamAssert.Ensure(
            drain.Result == "Drained",
            $"MON-A4 replacement source returned {drain.Result}/{drain.Reason}.");
        var firstRemoved = await WaitForEvidenceAsync(
            observer,
            ["kind=TopologyChanged", $"removed={failoverRid}", "kind=ServiceSummaryChanged"],
            removeBaseline);
        await first.DisposeAsync();
        var disconnected = await WaitForEvidenceAsync(
            observer,
            ["source=monitor.profile.client", "kind=Disconnected", first.ChannelEndpoint],
            removeBaseline);
        ZlinkStreamAssert.Ensure(
            HasSocketTransition(disconnected, first.ChannelEndpoint, "Disconnected", "Closed"),
            "MON-A4 old failover endpoint did not produce a disconnect transition.");

        var replaceBaseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;
        await using var replacement = await EphemeralService.StartAsync(options, failoverRid);
        ZlinkStreamAssert.Ensure(replacement.ChannelEndpoint != first.ChannelEndpoint,
            "MON-A4 replacement reused the old endpoint.");
        var replaced = await WaitForEvidenceAsync(
            observer,
            ["kind=TopologyChanged", $"added={failoverRid}", "kind=ServiceSummaryChanged",
                "source=monitor.profile.client", replacement.ChannelEndpoint],
            replaceBaseline);
        ZlinkStreamAssert.Ensure(
            HasSocketTransition(replaced, replacement.ChannelEndpoint, "Connected", "ConnectionReady"),
            "MON-A4 replacement endpoint did not produce a connection transition.");
    }

    private static async Task<ProfileRes> RequestAsync(ZLinkHttpClient service, ProfileReq request)
        => (await service.Post("/profile/request").Body(request).Async<ProfileRes>()).Body;

    private static async Task<string[]> WaitForEvidenceAsync(
        ZLinkHttpClient service,
        string[] contains,
        int afterIndex = 0)
        => (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(contains, [], TimeoutMilliseconds: 30000, AfterIndex: afterIndex))
            .Async<string[]>()).Body;

    private static bool HasAdmission(IEnumerable<string> evidence, string endpoint, uint value)
        => evidence.Any(line =>
            line.Contains("source=monitor.profile.client", StringComparison.Ordinal)
            && line.Contains("kind=PeerAdmissionChanged", StringComparison.Ordinal)
            && line.Contains($"remote={endpoint}", StringComparison.Ordinal)
            && line.Contains($"value={value}", StringComparison.Ordinal));

    private static bool HasSocketTransition(
        IEnumerable<string> evidence,
        string endpoint,
        params string[] kinds)
        => evidence.Any(line =>
            line.Contains("source=monitor.profile.client", StringComparison.Ordinal)
            && line.Contains($"remote={endpoint}", StringComparison.Ordinal)
            && kinds.Any(kind => line.Contains($"kind={kind}", StringComparison.Ordinal)));
}
