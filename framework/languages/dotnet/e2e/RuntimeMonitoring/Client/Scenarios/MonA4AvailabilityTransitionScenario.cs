// Verifies normal replacement and crash recovery against public runtime snapshots and events.
using System.Diagnostics;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA4AvailabilityTransitionScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var observer = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(35))
            .Build();

        await VerifyNormalReplacementAsync(options, observer);
        await VerifyCrashRecoveryAsync(options, observer);

        Console.WriteLine("scenario MON-A4 passed");
    }

    private static async Task VerifyNormalReplacementAsync(
        ClientOptions options,
        ZLinkHttpClient observer)
    {
        const string rid = "svc-a4-normal";
        var evidenceBaseline = await EvidenceCountAsync(observer);
        ulong firstGeneration;
        string firstEndpoint;

        await using (var first = await EphemeralService.StartAsync(options, rid))
        {
            var ready = await WaitForReadyPeerAsync(observer, rid);
            var peer = ready.Peers.Single(candidate => candidate.Rid == rid && candidate.Ready);
            firstGeneration = peer.LifecycleGeneration;
            firstEndpoint = peer.Endpoint;
            ZlinkStreamAssert.Ensure(
                peer.DescriptorRevision > 0
                && firstEndpoint == first.ChannelEndpoint
                && Channel(ready).ReadyMemberCount >= 2,
                "MON-A4 normal lifetime was not represented by the ready snapshot.");

            var drain = await first.DrainAsync();
            ZlinkStreamAssert.Ensure(
                drain.Result == "Drained",
                $"MON-A4 normal source returned {drain.Result}/{drain.Reason}.");
        }

        var removed = await WaitUntilNotReadyAsync(observer, rid, firstGeneration);
        ZlinkStreamAssert.Ensure(
            !removed.Peers.Any(peer =>
                peer.Rid == rid
                && peer.LifecycleGeneration == firstGeneration
                && peer.Ready),
            "MON-A4 drained lifetime remained ready.");

        await using var replacement = await EphemeralService.StartAsync(options, rid);
        var restored = await WaitForReadyPeerAsync(observer, rid);
        var replacementPeer = restored.Peers.Single(peer => peer.Rid == rid && peer.Ready);
        ZlinkStreamAssert.Ensure(
            replacementPeer.LifecycleGeneration != firstGeneration
            && replacementPeer.Endpoint != firstEndpoint
            && replacementPeer.Endpoint == replacement.ChannelEndpoint
            && Channel(restored).ReadyMemberCount >= 2,
            "MON-A4 normal replacement did not converge to the new lifetime.");
        await AssertPeerEventSequenceAsync(
            observer,
            evidenceBaseline,
            rid,
            replacementPeer.LifecycleGeneration);
    }

    private static async Task VerifyCrashRecoveryAsync(
        ClientOptions options,
        ZLinkHttpClient observer)
    {
        var beforeCrash = await WaitForReadyPeerAsync(observer, "svc-b");
        var crashedPeer = beforeCrash.Peers.Single(peer => peer.Rid == "svc-b" && peer.Ready);
        var evidenceBaseline = await EvidenceCountAsync(observer);

        using (var process = Process.GetProcessById(options.ServiceBProcessId))
        {
            process.Kill(entireProcessTree: true);
            await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));
        }

        var unavailable = await WaitUntilNotReadyAsync(
            observer,
            "svc-b",
            crashedPeer.LifecycleGeneration);
        ZlinkStreamAssert.Ensure(
            !unavailable.Peers.Any(peer =>
                peer.Rid == "svc-b"
                && peer.LifecycleGeneration == crashedPeer.LifecycleGeneration
                && peer.Ready),
            "MON-A4 crashed lifetime remained a successful ready route.");

        // The continuously running local provider makes this a deterministic
        // bounded operation while proving that the dead descriptor is not selected.
        var started = Stopwatch.GetTimestamp();
        var followUp = await observer.Post("/profile/request")
            .Body(new ProfileReq("after-crash", "mon-a4-after-crash"))
            .Async<ProfileRes>();
        var elapsed = Stopwatch.GetElapsedTime(started);
        ZlinkStreamAssert.Ensure(
            elapsed < TimeSpan.FromSeconds(3)
            && followUp.Body.ProviderRid == "svc-a",
            "MON-A4 follow-up request did not reach a bounded live provider.");

        await using var replacement = await EphemeralService.StartAsync(options, "svc-b");
        var restored = await WaitForReadyPeerAsync(observer, "svc-b");
        var replacementPeer = restored.Peers.Single(peer => peer.Rid == "svc-b" && peer.Ready);
        ZlinkStreamAssert.Ensure(
            replacementPeer.LifecycleGeneration != crashedPeer.LifecycleGeneration
            && replacementPeer.Endpoint != crashedPeer.Endpoint
            && replacementPeer.Endpoint == replacement.ChannelEndpoint
            && Channel(restored).ReadyMemberCount >= 2,
            "MON-A4 crash replacement did not restore the latest ready topology.");
        await AssertPeerEventSequenceAsync(
            observer,
            evidenceBaseline,
            "svc-b",
            replacementPeer.LifecycleGeneration);
    }

    private static MeshRuntimeChannelRes Channel(MeshRuntimeSnapshotRes snapshot)
        => snapshot.Channels.Single(channel =>
            channel.ChannelName == RuntimeMonitoringNames.Channel);

    private static async Task<int> EvidenceCountAsync(ZLinkHttpClient service)
        => (await service.Get("/evidence").Async<string[]>()).Body.Length;

    private static async Task<MeshRuntimeSnapshotRes> SnapshotAsync(ZLinkHttpClient service)
        => (await service.Get(
                $"/runtime/snapshot/{RuntimeMonitoringNames.Channel}")
            .Async<MeshRuntimeSnapshotRes>()).Body;

    private static async Task<MeshRuntimeSnapshotRes> WaitForReadyPeerAsync(
        ZLinkHttpClient service,
        string rid)
    {
        var elapsed = Stopwatch.StartNew();
        while (true)
        {
            var snapshot = await SnapshotAsync(service);
            if (snapshot.Peers.Any(peer => peer.Rid == rid && peer.Ready))
                return snapshot;
            if (elapsed.Elapsed >= TimeSpan.FromSeconds(3))
                throw new InvalidOperationException(
                    $"MON-A4 peer '{rid}' did not become ready.");
            await Task.Delay(TimeSpan.FromMilliseconds(50));
        }
    }

    private static async Task<MeshRuntimeSnapshotRes> WaitUntilNotReadyAsync(
        ZLinkHttpClient service,
        string rid,
        ulong generation)
    {
        var elapsed = Stopwatch.StartNew();
        while (true)
        {
            var snapshot = await SnapshotAsync(service);
            if (!snapshot.Peers.Any(peer =>
                    peer.Rid == rid
                    && peer.LifecycleGeneration == generation
                    && peer.Ready))
                return snapshot;
            if (elapsed.Elapsed >= TimeSpan.FromSeconds(3))
                throw new InvalidOperationException(
                    $"MON-A4 peer '{rid}' generation {generation} remained ready.");
            await Task.Delay(TimeSpan.FromMilliseconds(50));
        }
    }

    private static async Task AssertPeerEventSequenceAsync(
        ZLinkHttpClient service,
        int afterIndex,
        string rid,
        ulong replacementGeneration)
    {
        var evidence = (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                [
                    "identifier=zlink.runtime.mesh_node.peer_changed",
                    $"routing={rid}",
                    $"generation={replacementGeneration}"
                ],
                [],
                TimeoutMilliseconds: 3000,
                AfterIndex: afterIndex))
            .Async<string[]>()).Body;
        var sequence = evidence
            .Where(line =>
                line.Contains(
                    $"source={RuntimeMonitoringNames.Channel}",
                    StringComparison.Ordinal)
                && line.Contains(
                    "identifier=zlink.runtime.mesh_node.peer_changed",
                    StringComparison.Ordinal)
                && line.Contains($"routing={rid}", StringComparison.Ordinal))
            .Select(ParseSequence)
            .ToArray();
        ZlinkStreamAssert.Ensure(
            sequence.Length >= 2
            && sequence.All(value => value > 0)
            && sequence.Zip(sequence.Skip(1), static (left, right) => right > left)
                .All(static increasing => increasing),
            $"MON-A4 peer event sequence for '{rid}' was not strictly increasing.");
    }

    private static ulong ParseSequence(string line)
    {
        const string prefix = "|sequence=";
        var start = line.IndexOf(prefix, StringComparison.Ordinal);
        if (start < 0) return 0;
        start += prefix.Length;
        var end = line.IndexOf('|', start);
        var text = end < 0 ? line[start..] : line[start..end];
        return ulong.TryParse(text, out var value) ? value : 0;
    }
}
