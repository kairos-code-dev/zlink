// Verifies Config 7 MON-A2 peer admission and same-RID lifetime replacement.
using System.Diagnostics;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA2RegistryEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var observer = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(35))
            .Build();
        var evidenceBaseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;

        ulong firstGeneration;
        string firstEndpoint;
        await using (var first = await EphemeralService.StartAsync(options, "svc-b"))
        {
            var firstReady = await WaitForReadyPeerAsync(observer, "svc-b");
            var peer = firstReady.Peers.Single(candidate =>
                candidate.Rid == "svc-b" && candidate.Ready);
            firstGeneration = peer.LifecycleGeneration;
            firstEndpoint = peer.Endpoint;
            ZlinkStreamAssert.Ensure(
                firstGeneration > 0
                && peer.DescriptorRevision > 0
                && peer.Endpoint == first.ChannelEndpoint
                && peer.AdmissionState == "ready"
                && peer.LastFailure is null,
                "MON-A2 first admitted lifetime fields were incomplete.");
        }

        await WaitUntilNotReadyAsync(observer, "svc-b", firstGeneration);

        await using var replacement = await EphemeralService.StartAsync(options, "svc-b");
        var replacementReady = await WaitForReadyPeerAsync(observer, "svc-b");
        var replacementPeer = replacementReady.Peers.Single(candidate =>
            candidate.Rid == "svc-b" && candidate.Ready);
        ZlinkStreamAssert.Ensure(
            replacementPeer.LifecycleGeneration != firstGeneration
            && replacementPeer.Endpoint != firstEndpoint
            && replacementPeer.Endpoint == replacement.ChannelEndpoint,
            "MON-A2 replacement did not expose a new generation and endpoint.");
        ZlinkStreamAssert.Ensure(
            !replacementReady.Peers.Any(peer =>
                peer.Rid == "svc-b"
                && peer.LifecycleGeneration == firstGeneration
                && peer.Ready),
            "MON-A2 old peer lifetime remained ready after replacement.");

        var evidence = (await observer.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                [
                    "identifier=zlink.runtime.mesh_node.peer_changed",
                    "routing=svc-b",
                    "kind=ConnectionReady"
                ],
                [],
                TimeoutMilliseconds: 3000,
                AfterIndex: evidenceBaseline))
            .Async<string[]>()).Body;
        var sequences = evidence
            .Where(line =>
                line.Contains("identifier=zlink.runtime.mesh_node.peer_changed",
                    StringComparison.Ordinal)
                && line.Contains(
                    $"source={RuntimeMonitoringNames.Channel}",
                    StringComparison.Ordinal)
                && line.Contains("routing=svc-b", StringComparison.Ordinal))
            .Select(ParseSequence)
            .ToArray();
        ZlinkStreamAssert.Ensure(
            sequences.Length >= 3
            && sequences.Zip(sequences.Skip(1), static (left, right) => right > left)
                .All(static increasing => increasing),
            "MON-A2 peer event sequence was not strictly increasing.");
        ZlinkStreamAssert.Ensure(
            evidence.Any(line => line.Contains(
                $"generation={replacementPeer.LifecycleGeneration}",
                StringComparison.Ordinal)),
            "MON-A2 replacement event did not carry the new lifecycle generation.");

        Console.WriteLine("scenario MON-A2 passed");
    }

    private static async Task<MeshRuntimeSnapshotRes> SnapshotAsync(
        ZLinkHttpClient service)
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
                    $"MON-A2 peer '{rid}' did not become ready.");
            await Task.Delay(TimeSpan.FromMilliseconds(50));
        }
    }

    private static async Task WaitUntilNotReadyAsync(
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
                return;
            if (elapsed.Elapsed >= TimeSpan.FromSeconds(3))
                throw new InvalidOperationException(
                    $"MON-A2 peer '{rid}' generation {generation} remained ready.");
            await Task.Delay(TimeSpan.FromMilliseconds(50));
        }
    }

    private static ulong ParseSequence(string line)
    {
        const string prefix = "|sequence=";
        var start = line.IndexOf(prefix, StringComparison.Ordinal);
        if (start < 0)
            return 0;
        start += prefix.Length;
        var end = line.IndexOf('|', start);
        var text = end < 0 ? line[start..] : line[start..end];
        return ulong.TryParse(text, out var value) ? value : 0;
    }
}
