using Systems.Zlink;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Configuration;

public sealed class RouteMeshRuntimeContracts
{
    [Fact]
    [ContractExample(typeof(IZLinkRouteMeshRuntime))]
    public async Task Mesh_runtime_surfaces_one_snapshot_and_event_stream_per_mesh()
    {
        IZLinkRouteMeshRuntime meshRuntime = new ExampleMeshRuntime();

        var snapshot = meshRuntime.Snapshot("orders");
        Assert.Equal("orders", snapshot.MeshName);
        Assert.Equal(ZLinkMeshNodeState.Serving, snapshot.State);
        var peer = Assert.Single(snapshot.Peers);
        Assert.True(peer.Ready);
        var channel = Assert.Single(snapshot.Channels);
        Assert.True(channel.Selectable);
        Assert.Equal(new ZLinkPopulationCapacity(4, 2, 0),
            snapshot.PopulationCapacity.Actors);
        Assert.Equal(new ZLinkPopulationCapacity(3, 1, 20),
            snapshot.PopulationCapacity.Spots);
        Assert.Equal(new ZLinkActivationConcurrency(2, 8),
            snapshot.ActivationConcurrency);

        Assert.True(meshRuntime.IsReady("orders"));

        await foreach (var runtimeEvent in meshRuntime.ObserveAsync("orders", capacity: 16))
        {
            Assert.Equal("zlink.runtime.mesh_node.peer_changed", runtimeEvent.Identifier);
            Assert.Equal("orders", runtimeEvent.MeshName);
            Assert.Equal(peer.Rid, runtimeEvent.PeerRid);
            break;
        }
        Assert.DoesNotContain(
            typeof(IZLinkRouteMeshRuntime).GetMethods(),
            static method => method.Name is "DrainAsync" or "AwaitDrainedAsync");
    }

    private sealed class ExampleMeshRuntime : IZLinkRouteMeshRuntime
    {
        private static readonly RoutingId NodeRid = RoutingId.From("orders-a");
        private static readonly RoutingId PeerRid = RoutingId.From("orders-b");
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
                new ZLinkMeshClaimSnapshot(
                    ApplicationActive: true, 0, InfrastructureActive: true, 0),
                new ZLinkLocationRuntimeSnapshot("ready", DateTimeOffset.UtcNow, null))
            {
                PopulationCapacity = new ZLinkPlacementCapacity(
                    new ZLinkPopulationCapacity(4, 2, 0),
                    new ZLinkPopulationCapacity(3, 1, 20),
                    [
                        new ZLinkSpotTypeCapacity(
                            ZLinkPlacementObjectKind.UserSpot,
                            "room",
                            2,
                            1,
                            10)
                    ]),
                ActivationConcurrency = new ZLinkActivationConcurrency(2, 8)
            };
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
                PlacementOutcome: null,
                Capacity: null,
                PopulationCapacity: null,
                ActivationConcurrency: null,
                Reason: "ready",
                State: null);
        }

        public bool IsReady(string meshName) => true;
    }
}
