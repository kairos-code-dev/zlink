using Systems.Zlink;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Configuration;

public sealed class RouteMeshRuntimeContracts
{
    [Fact]
    [ContractExample(typeof(IZLinkRouteMeshRuntime))]
    public async Task Mesh_runtime_surfaces_one_snapshot_event_stream_and_shared_drain_per_mesh()
    {
        IZLinkRouteMeshRuntime meshRuntime = new ExampleMeshRuntime();

        var snapshot = meshRuntime.Snapshot("orders");
        Assert.Equal("orders", snapshot.MeshName);
        Assert.Equal(ZLinkMeshNodeState.Serving, snapshot.State);
        var peer = Assert.Single(snapshot.Peers);
        Assert.True(peer.Ready);
        var channel = Assert.Single(snapshot.Channels);
        Assert.True(channel.Selectable);

        Assert.True(meshRuntime.IsReady("orders"));

        await foreach (var runtimeEvent in meshRuntime.ObserveAsync("orders", capacity: 16))
        {
            Assert.Equal("zlink.runtime.mesh_node.peer_changed", runtimeEvent.Identifier);
            Assert.Equal("orders", runtimeEvent.MeshName);
            Assert.Equal(peer.Rid, runtimeEvent.PeerRid);
            break;
        }

        var drained = await meshRuntime.DrainAsync("orders", TimeSpan.FromSeconds(5));
        Assert.IsType<ZLinkMeshDrainResult.Drained>(drained);
        Assert.Same(drained, await meshRuntime.AwaitDrainedAsync("orders"));
    }

    private sealed class ExampleMeshRuntime : IZLinkRouteMeshRuntime
    {
        private static readonly RoutingId NodeRid = RoutingId.From("orders-a");
        private static readonly RoutingId PeerRid = RoutingId.From("orders-b");
        private ZLinkMeshDrainResult? _terminal;

        public ZLinkMeshNodeSnapshot Snapshot(string meshName)
        {
            return new ZLinkMeshNodeSnapshot(
                meshName,
                NodeRid,
                LifecycleGeneration: 7,
                DescriptorRevision: 3,
                "tcp://127.0.0.1:5400",
                ZLinkMeshNodeState.Serving,
                Sequence: 1,
                DateTimeOffset.UtcNow,
                ["redis"],
                [
                    new ZLinkMeshPeerSnapshot(
                        PeerRid,
                        LifecycleGeneration: 5,
                        DescriptorRevision: 2,
                        "tcp://127.0.0.1:5401",
                        "ready",
                        Ready: true,
                        "serving",
                        ["orders"],
                        LastFailure: null)
                ],
                [new ZLinkMeshChannelSnapshot("orders", LocalWeight: 100, ReadyMemberCount: 2, Selectable: true)],
                new ZLinkLogicalMulticastSnapshot(
                    0, 0, 0, 0, 0, 0, 0, 0, 0),
                new ZLinkMeshClaimSnapshot(
                    ApplicationActive: true, 0, InfrastructureActive: true, 0),
                new ZLinkLocationRuntimeSnapshot("ready", DateTimeOffset.UtcNow, null),
                new ZLinkMeshDrainSnapshot(
                    ZLinkMeshNodeState.Serving, null, WorkSealed: false, 0, 0, 0));
        }

        public async IAsyncEnumerable<ZLinkMeshRuntimeEvent> ObserveAsync(
            string meshName,
            int capacity = 1024,
            [System.Runtime.CompilerServices.EnumeratorCancellation]
            CancellationToken cancellationToken = default)
        {
            await Task.Yield();
            yield return new ZLinkMeshRuntimeEvent(
                "zlink.runtime.mesh_node.peer_changed",
                Sequence: 2,
                DateTimeOffset.UtcNow,
                meshName,
                NodeRid,
                PeerRid,
                LifecycleGeneration: 5,
                DescriptorRevision: 2,
                ChannelName: null,
                ClaimDomain: null,
                MessageKind: null,
                RemoteSnapshotCount: null,
                RemoteAdmittedCount: null,
                RemoteDroppedCount: null,
                LocalSnapshotCount: null,
                LocalAdmittedCount: null,
                LocalDroppedCount: null,
                Reason: "ready",
                State: null);
        }

        public bool IsReady(string meshName) => true;

        // The first drain call fixes the shared terminal; later calls and
        // AwaitDrainedAsync observe the same result instance.
        public ValueTask<ZLinkMeshDrainResult> DrainAsync(
            string meshName,
            TimeSpan? deadline = null,
            CancellationToken cancellationToken = default)
        {
            _terminal ??= new ZLinkMeshDrainResult.Drained();
            return ValueTask.FromResult(_terminal);
        }

        public ValueTask<ZLinkMeshDrainResult> AwaitDrainedAsync(
            string meshName,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult(
                _terminal ?? throw new InvalidOperationException("Drain has not started."));
        }
    }
}
