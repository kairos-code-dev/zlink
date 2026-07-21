// Verifies Redis location health degradation and recovery through the public runtime.
using System.Diagnostics;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA5FixedKindsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var service = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(35))
            .Build();
        var baseline = await WaitForLocationStateAsync(service, "ready");
        var baselineEvidence = (await service.Get("/evidence").Async<string[]>()).Body.Length;
        ZlinkStreamAssert.Ensure(
            baseline.Location.LastSuccessAt is not null
            && baseline.Location.LastFailureAt is null,
            "MON-A5 normal location health timestamps were incomplete.");

        await RunDockerAsync("pause", options.RedisContainer);
        try
        {
            var degraded = await WaitForLocationStateAsync(service, "degraded");
            // Wall-clock timestamps may move when the host clock is corrected.
            // State plus the sequenced runtime event below proves transition order.
            ZlinkStreamAssert.Ensure(
                degraded.Location.LastFailureAt is not null,
                "MON-A5 degraded snapshot did not record the store failure: "
                + $"lastSuccess={baseline.Location.LastSuccessAt:O}, "
                + $"lastFailure={degraded.Location.LastFailureAt:O}.");
            ZlinkStreamAssert.Ensure(
                degraded.Peers.Any(peer => peer.Rid == "svc-b" && peer.Ready)
                && degraded.Channels.Single(channel =>
                    channel.ChannelName == RuntimeMonitoringNames.Channel).Selectable,
                "MON-A5 store outage incorrectly removed the admitted messaging path.");

            var request = await service.Post("/profile/request")
                .Body(new ProfileReq("during-outage", "mon-a5-during-outage"))
                .Async<ProfileRes>();
            ZlinkStreamAssert.Ensure(
                request.Body.Marker == "mon-a5-during-outage",
                "MON-A5 admitted messaging did not continue during the store outage.");
            await WaitForLocationEventAsync(
                service,
                baselineEvidence,
                "degraded");
        }
        finally
        {
            await RunDockerAsync("unpause", options.RedisContainer);
        }

        var recovered = await WaitForLocationStateAsync(service, "ready");
        // The public event sequence, rather than wall-clock comparison, orders recovery.
        ZlinkStreamAssert.Ensure(
            recovered.Location.LastSuccessAt is not null
            && recovered.Location.LastFailureAt is not null
            && recovered.Peers.Any(peer => peer.Rid == "svc-b" && peer.Ready),
            "MON-A5 recovered snapshot did not revalidate the current ready topology: "
            + $"lastSuccess={recovered.Location.LastSuccessAt:O}, "
            + $"lastFailure={recovered.Location.LastFailureAt:O}.");
        await WaitForLocationEventAsync(service, baselineEvidence, "ready");

        Console.WriteLine("scenario MON-A5 passed");
    }

    private static async Task<MeshRuntimeSnapshotRes> WaitForLocationStateAsync(
        ZLinkHttpClient service,
        string state)
    {
        var elapsed = Stopwatch.StartNew();
        while (true)
        {
            var snapshot = (await service.Get(
                    $"/runtime/snapshot/{RuntimeMonitoringNames.Channel}")
                .Async<MeshRuntimeSnapshotRes>()).Body;
            if (snapshot.Location.State == state)
                return snapshot;
            if (elapsed.Elapsed >= TimeSpan.FromSeconds(3))
                throw new InvalidOperationException(
                    $"MON-A5 location state did not become '{state}'.");
            await Task.Delay(TimeSpan.FromMilliseconds(100));
        }
    }

    private static async Task WaitForLocationEventAsync(
        ZLinkHttpClient service,
        int afterIndex,
        string state)
    {
        var evidence = (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                [
                    $"source={RuntimeMonitoringNames.Channel}",
                    "identifier=zlink.runtime.location.store_changed",
                    $"reason={state}"
                ],
                [],
                TimeoutMilliseconds: 3000,
                AfterIndex: afterIndex))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            evidence.Any(line =>
                line.Contains(
                    "identifier=zlink.runtime.location.store_changed",
                    StringComparison.Ordinal)
                && line.Contains($"reason={state}", StringComparison.Ordinal)),
            $"MON-A5 '{state}' location event was missing.");
    }

    private static async Task RunDockerAsync(string verb, string container)
    {
        var start = new ProcessStartInfo("docker")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        start.ArgumentList.Add(verb);
        start.ArgumentList.Add(container);
        using var process = Process.Start(start)
                            ?? throw new InvalidOperationException(
                                $"Failed to run docker {verb}.");
        await process.WaitForExitAsync();
        if (process.ExitCode == 0)
            return;
        var error = await process.StandardError.ReadToEndAsync();
        throw new InvalidOperationException(
            $"docker {verb} {container} failed: {error}");
    }
}
