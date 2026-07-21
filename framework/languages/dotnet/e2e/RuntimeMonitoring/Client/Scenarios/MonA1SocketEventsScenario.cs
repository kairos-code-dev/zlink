// Verifies Config 7 MON-A1 with two immutable public MeshNode snapshots.
using System.Diagnostics;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA1SocketEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var observer = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(35))
            .Build();

        var baseline = await SnapshotAsync(observer);
        AssertCompleteBaseline(baseline);
        var frozenBaseline = baseline;
        var evidenceBaseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;

        await using var serviceB = await EphemeralService.StartAsync(options, "svc-b");
        var ready = await WaitForPeerAsync(observer, "svc-b", ready: true);
        var peer = ready.Peers.Single(candidate => candidate.Rid == "svc-b" && candidate.Ready);

        ZlinkStreamAssert.Ensure(
            ready.Sequence > baseline.Sequence,
            "MON-A1 second snapshot sequence did not increase.");
        ZlinkStreamAssert.Ensure(
            frozenBaseline.Peers.Length == 0
            && frozenBaseline.Sequence == baseline.Sequence
            && frozenBaseline.Endpoint == baseline.Endpoint,
            "MON-A1 first snapshot changed after the second snapshot call.");
        ZlinkStreamAssert.Ensure(
            peer.LifecycleGeneration > 0
            && peer.DescriptorRevision > 0
            && peer.Endpoint == serviceB.ChannelEndpoint
            && peer.AdmissionState == "ready"
            && peer.ChannelNames.Contains(
                RuntimeMonitoringNames.Channel,
                StringComparer.Ordinal),
            "MON-A1 ready peer fields were incomplete.");

        var channel = ready.Channels.Single(candidate =>
            candidate.ChannelName == RuntimeMonitoringNames.Channel);
        ZlinkStreamAssert.Ensure(
            channel.LocalWeight == 100
            && channel.ReadyMemberCount == 2
            && channel.Selectable,
            "MON-A1 channel snapshot did not include both selectable members.");

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
        ZlinkStreamAssert.Ensure(
            evidence.Any(line =>
                line.Contains("identifier=zlink.runtime.mesh_node.peer_changed",
                    StringComparison.Ordinal)
                && line.Contains("routing=svc-b", StringComparison.Ordinal)
                && line.Contains("sequence=", StringComparison.Ordinal)),
            "MON-A1 typed peer event was not observed.");

        Console.WriteLine("scenario MON-A1 passed");
    }

    private static void AssertCompleteBaseline(MeshRuntimeSnapshotRes snapshot)
    {
        ZlinkStreamAssert.Ensure(
            snapshot.MeshName == RuntimeMonitoringNames.Channel
            && snapshot.Rid == "svc-a"
            && snapshot.LifecycleGeneration > 0
            && snapshot.DescriptorRevision > 0
            && snapshot.Endpoint.StartsWith("tcp://", StringComparison.Ordinal)
            && snapshot.State == "Serving"
            && snapshot.Sequence > 0
            && snapshot.ObservedAt != default
            && snapshot.DescriptorSources.Contains("redis", StringComparer.Ordinal)
            && snapshot.Peers.Length == 0,
            "MON-A1 baseline MeshNode identity or lifecycle fields were incomplete.");

        var channel = snapshot.Channels.Single(candidate =>
            candidate.ChannelName == RuntimeMonitoringNames.Channel);
        ZlinkStreamAssert.Ensure(
            channel.LocalWeight == 100
            && channel.ReadyMemberCount == 1
            && channel.Selectable,
            "MON-A1 baseline channel fields were incomplete.");
        ZlinkStreamAssert.Ensure(
            snapshot.Multicast.Submitted == 0
            && snapshot.Claims.ApplicationActive
            && snapshot.Claims.InfrastructureActive
            && snapshot.Location.State == "ready"
            && snapshot.Drain.State == "Serving"
            && !snapshot.Drain.WorkSealed,
            "MON-A1 multicast, claim, location or drain fields were incomplete.");
    }

    private static async Task<MeshRuntimeSnapshotRes> SnapshotAsync(
        ZLinkHttpClient service)
        => (await service.Get(
                $"/runtime/snapshot/{RuntimeMonitoringNames.Channel}")
            .Async<MeshRuntimeSnapshotRes>()).Body;

    private static async Task<MeshRuntimeSnapshotRes> WaitForPeerAsync(
        ZLinkHttpClient service,
        string rid,
        bool ready)
    {
        var elapsed = Stopwatch.StartNew();
        while (true)
        {
            var snapshot = await SnapshotAsync(service);
            if (snapshot.Peers.Any(peer => peer.Rid == rid && peer.Ready == ready))
                return snapshot;
            if (elapsed.Elapsed >= TimeSpan.FromSeconds(3))
                throw new InvalidOperationException(
                    $"MON-A1 peer '{rid}' did not reach ready={ready}.");
            await Task.Delay(TimeSpan.FromMilliseconds(50));
        }
    }
}
