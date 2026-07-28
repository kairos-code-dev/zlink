using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Net;
using System.Net.Sockets;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class RouteMeshRuntimeServiceTests
{
    [Fact]
    public async Task Missing_Required_Descriptor_Peer_Degrades_Topology()
    {
        await using var fixture = await RuntimeFixture.StartAsync(
            ZLinkMeshNodeObjectRole.Server);
        var remoteRid = RoutingId.From("aa-required-server");

        await fixture.PublishDescriptorAsync(
            remoteRid,
            ZLinkMeshNodeObjectRole.Server);

        var status = await WaitForStatusAsync(
            fixture.Runtime,
            candidate => candidate.Peers.Any(peer =>
                peer.NodeRid == remoteRid
                && peer.State == ZLinkPeerState.NotConnected));

        Assert.Equal(ZLinkTopologyState.Degraded, status.State);
        Assert.False(status.IsReady);
        Assert.True(status.Placement.IsAvailable);
    }

    [Fact]
    public async Task Missing_ObjectClient_Descriptor_Is_NotRequired_And_Remains_Ready()
    {
        await using var fixture = await RuntimeFixture.StartAsync(
            ZLinkMeshNodeObjectRole.Client);
        var remoteRid = RoutingId.From("aa-client");

        await fixture.PublishDescriptorAsync(
            remoteRid,
            ZLinkMeshNodeObjectRole.Client);

        var status = await WaitForStatusAsync(
            fixture.Runtime,
            candidate => candidate.Peers.Any(peer =>
                peer.NodeRid == remoteRid
                && peer.State == ZLinkPeerState.NotRequired));

        Assert.Equal(ZLinkTopologyState.Ready, status.State);
        Assert.True(status.IsReady);
        Assert.Equal(0, status.ReadyPeerCount);
    }

    [Fact]
    public async Task Automatic_ObjectClient_Target_Is_NotFound_Without_New_Connection_Intent()
    {
        await using var fixture = await RuntimeFixture.StartAsync(
            ZLinkMeshNodeObjectRole.Client);
        var remoteRid = RoutingId.From("aa-client-target");

        await fixture.PublishDescriptorAsync(
            remoteRid,
            ZLinkMeshNodeObjectRole.Client);
        await WaitForStatusAsync(
            fixture.Runtime,
            candidate => candidate.Peers.Any(peer =>
                peer.NodeRid == remoteRid
                && peer.State == ZLinkPeerState.NotRequired));
        var peerCount = fixture.Runtime.GetStatus(RuntimeFixture.MeshName).Peers.Count;

        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            async () => await fixture.RouteClient
                .RequestToNode(
                    RuntimeFixture.MeshName,
                    remoteRid,
                    new RouteProbe("automatic"))
                .Async<RouteProbe>());
        var sendError = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            async () => await fixture.RouteClient
                .SendToNode(
                    RuntimeFixture.MeshName,
                    remoteRid,
                    new RouteProbe("automatic-send"))
                .Async());

        Assert.Equal(ZLinkFrameworkErrorKind.NotFound, error.Kind);
        Assert.Equal(
            ZLinkFrameworkErrorKind.NotFound,
            sendError.Kind);
        await Task.Delay(100);
        var status = fixture.Runtime.GetStatus(RuntimeFixture.MeshName);
        Assert.Equal(peerCount, status.Peers.Count);
        Assert.Single(
            status.Peers,
            peer => peer.NodeRid == remoteRid
                    && peer.State == ZLinkPeerState.NotRequired);
    }

    [Fact]
    public async Task Required_Server_To_Client_Peer_Is_NotConnected_But_Not_A_Node_Target()
    {
        await using var fixture = await RuntimeFixture.StartAsync(
            ZLinkMeshNodeObjectRole.Server);
        var remoteRid = RoutingId.From("aa-required-client");

        await fixture.PublishDescriptorAsync(
            remoteRid,
            ZLinkMeshNodeObjectRole.Client);
        var status = await WaitForStatusAsync(
            fixture.Runtime,
            candidate => candidate.Peers.Any(peer =>
                peer.NodeRid == remoteRid
                && peer.State == ZLinkPeerState.NotConnected));

        Assert.Equal(ZLinkTopologyState.Degraded, status.State);
        Assert.DoesNotContain(
            status.Peers,
            peer => peer.NodeRid == remoteRid
                    && peer.State == ZLinkPeerState.NotRequired);

        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            async () => await fixture.RouteClient
                .RequestToNode(
                    RuntimeFixture.MeshName,
                    remoteRid,
                    new RouteProbe("required-client"))
                .Async<RouteProbe>());

        Assert.Equal(ZLinkFrameworkErrorKind.NotFound, error.Kind);
    }

    [Fact]
    public async Task Connecting_Expected_Peer_Has_One_Public_Identity()
    {
        await using var fixture = await RuntimeFixture.StartAsync(
            ZLinkMeshNodeObjectRole.Server,
            routingIdPrefix: "aa-runtime");
        var remoteRid = RoutingId.From("zz-required-server");

        await fixture.PublishDescriptorAsync(
            remoteRid,
            ZLinkMeshNodeObjectRole.Server);

        var status = await WaitForStatusAsync(
            fixture.Runtime,
            candidate => candidate.Peers.Any(peer =>
                peer.NodeRid == remoteRid
                && peer.State == ZLinkPeerState.Connecting));

        Assert.Single(status.Peers, peer => peer.NodeRid == remoteRid);
        Assert.DoesNotContain(status.Peers, peer => peer.NodeRid.IsEmpty);
        Assert.Equal(ZLinkTopologyState.Degraded, status.State);
    }

    [Fact]
    public async Task Manual_ObjectClient_Target_Is_NotFound_Without_Retrying_The_Pair()
    {
        var targetEndpoint = RuntimeFixture.ReserveTcpEndpoint();
        await using var target = await RuntimeFixture.StartAsync(
            ZLinkMeshNodeObjectRole.Client,
            listenEndpoint: targetEndpoint);
        await using var source = await RuntimeFixture.StartManualAsync(
            ZLinkMeshNodeObjectRole.Client,
            target.LocalNodeRid,
            target.ListenEndpoint);

        await WaitForStatusAsync(
            source.Runtime,
            candidate => candidate.Peers.Any(peer =>
                peer.NodeRid == target.LocalNodeRid
                && peer.State == ZLinkPeerState.NotRequired));
        var peerCount = source.Runtime.GetStatus(RuntimeFixture.MeshName).Peers.Count;

        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            async () => await source.RouteClient
                .RequestToNode(
                    RuntimeFixture.MeshName,
                    target.LocalNodeRid,
                    new RouteProbe("manual"))
                .Async<RouteProbe>());

        Assert.Equal(ZLinkFrameworkErrorKind.NotFound, error.Kind);
        await Task.Delay(1100);
        var status = source.Runtime.GetStatus(RuntimeFixture.MeshName);
        Assert.Equal(peerCount, status.Peers.Count);
        Assert.Single(
            status.Peers,
            peer => peer.NodeRid == target.LocalNodeRid
                    && peer.State == ZLinkPeerState.NotRequired);
    }

    [Fact]
    public async Task Descriptor_Add_And_Remove_Wake_ObserveAsync()
    {
        await using var fixture = await RuntimeFixture.StartAsync(
            ZLinkMeshNodeObjectRole.Client);
        var remoteRid = RoutingId.From("aa-observed-client");
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        await using var observer = fixture.Runtime
            .ObserveAsync(RuntimeFixture.MeshName, timeout.Token)
            .GetAsyncEnumerator(timeout.Token);

        Assert.True(await observer.MoveNextAsync());
        await fixture.PublishDescriptorAsync(
            remoteRid,
            ZLinkMeshNodeObjectRole.Client);
        var added = await MoveUntilAsync(
            observer,
            status => status.Peers.Any(peer => peer.NodeRid == remoteRid));
        Assert.Contains(
            added.Peers,
            peer => peer.NodeRid == remoteRid
                    && peer.State == ZLinkPeerState.NotRequired);

        await fixture.RemoveDescriptorAsync(remoteRid);
        var removed = await MoveUntilAsync(
            observer,
            status => status.Peers.All(peer => peer.NodeRid != remoteRid));
        Assert.DoesNotContain(removed.Peers, peer => peer.NodeRid == remoteRid);
    }

    private static async Task<ZLinkRouteMeshStatus> WaitForStatusAsync(
        IZLinkRouteMeshRuntime runtime,
        Func<ZLinkRouteMeshStatus, bool> predicate)
    {
        var deadline = DateTimeOffset.UtcNow.AddSeconds(5);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var status = runtime.GetStatus(RuntimeFixture.MeshName);
            if (predicate(status))
                return status;
            await Task.Delay(10);
        }
        var current = runtime.GetStatus(RuntimeFixture.MeshName);
        throw new TimeoutException(
            "RouteMesh status did not reach the expected state. "
            + string.Join(
                ", ",
                current.Peers.Select(peer =>
                    $"{peer.NodeRid}:{peer.State}")));
    }

    private static async Task<ZLinkRouteMeshStatus> MoveUntilAsync(
        IAsyncEnumerator<ZLinkRouteMeshStatus> observer,
        Func<ZLinkRouteMeshStatus, bool> predicate)
    {
        while (await observer.MoveNextAsync())
        {
            if (predicate(observer.Current))
                return observer.Current;
        }
        throw new InvalidOperationException(
            "RouteMesh observation completed before the expected status.");
    }

    private sealed class RuntimeFixture : IAsyncDisposable
    {
        internal const string MeshName = "orders";

        private readonly ServiceProvider _provider;
        private readonly IHostedService _hosted;
        private readonly ZLinkLocationRuntime _locations;

        private RuntimeFixture(
            ServiceProvider provider,
            IHostedService hosted,
            ZLinkLocationRuntime locations,
            IZLinkRouteMeshRuntime runtime,
            IZLinkRouteClient routeClient,
            RoutingId localNodeRid,
            string listenEndpoint)
        {
            _provider = provider;
            _hosted = hosted;
            _locations = locations;
            Runtime = runtime;
            RouteClient = routeClient;
            LocalNodeRid = localNodeRid;
            ListenEndpoint = listenEndpoint;
        }

        internal IZLinkRouteMeshRuntime Runtime { get; }

        internal IZLinkRouteClient RouteClient { get; }

        internal RoutingId LocalNodeRid { get; }

        internal string ListenEndpoint { get; }

        internal static async Task<RuntimeFixture> StartAsync(
            ZLinkMeshNodeObjectRole objectRole,
            string routingIdPrefix = "zz-runtime",
            string? listenEndpoint = null)
        {
            listenEndpoint ??= $"inproc://route-runtime-{Guid.NewGuid():N}";
            var services = new ServiceCollection();
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                var node = options.AddRouteMesh(MeshName)
                    .Listen(listenEndpoint)
                    .SetRoutingIdPrefix(routingIdPrefix);
                if (objectRole == ZLinkMeshNodeObjectRole.Client)
                    node.Objects().Client();
                else
                    node.Objects().Server();
            });
            return await StartProviderAsync(services, listenEndpoint);
        }

        internal static async Task<RuntimeFixture> StartManualAsync(
            ZLinkMeshNodeObjectRole objectRole,
            RoutingId peerRid,
            string peerEndpoint)
        {
            var listenEndpoint = ReserveTcpEndpoint();
            var services = new ServiceCollection();
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                var node = options.AddRouteMesh(MeshName)
                    .Listen(listenEndpoint);
                if (objectRole == ZLinkMeshNodeObjectRole.Client)
                    node.Objects().Client();
                else
                    node.Objects().Server();
                node.PeerConnections.Connect(peerRid, peerEndpoint);
            });
            return await StartProviderAsync(services, listenEndpoint);
        }

        internal static string ReserveTcpEndpoint()
        {
            var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            var port = ((IPEndPoint)listener.LocalEndpoint).Port;
            listener.Stop();
            return $"tcp://127.0.0.1:{port}";
        }

        private static async Task<RuntimeFixture> StartProviderAsync(
            ServiceCollection services,
            string listenEndpoint)
        {
            var provider = services.BuildServiceProvider();
            var hosted = provider.GetServices<IHostedService>().Single(
                static service => service is ZLinkFrameworkHostedService);
            try
            {
                await hosted.StartAsync(CancellationToken.None);
                var frameworkRuntime =
                    provider.GetRequiredService<ZLinkFrameworkRuntime>();
                return new RuntimeFixture(
                    provider,
                    hosted,
                    provider.GetRequiredService<ZLinkLocationRuntime>(),
                    provider.GetRequiredService<IZLinkRouteMeshRuntime>(),
                    provider.GetRequiredService<IZLinkRouteClient>(),
                    frameworkRuntime.GetMeshNodeRuntime(MeshName).Node.RoutingId,
                    listenEndpoint);
            }
            catch
            {
                await provider.DisposeAsync();
                throw;
            }
        }

        internal async Task PublishDescriptorAsync(
            RoutingId rid,
            ZLinkMeshNodeObjectRole objectRole)
        {
            var result = await _locations.WriteDescriptorAsync(
                new ZLinkMeshNodeDescriptor(
                    MeshName,
                    rid,
                    LifecycleGeneration: 1,
                    DescriptorRevision: 1,
                    $"inproc://missing-{Guid.NewGuid():N}",
                    new Dictionary<string, int>(StringComparer.Ordinal),
                    SecurityIdentity: string.Empty,
                    OwnerId: string.Empty,
                    LeaseGeneration: 0,
                    UpdatedAt: default)
                {
                    ObjectRole = objectRole,
                    State = ZLinkFrameworkRuntimeState.Serving,
                    EntrySpotId = objectRole == ZLinkMeshNodeObjectRole.Server
                        ? $"{rid.ToHex()}-entry"
                        : null
                },
                ZLinkLocationWriteIntent.NewClaim);
            Assert.Equal(ZLinkLocationWriteStatus.Stored, result.Status);
        }

        internal async Task RemoveDescriptorAsync(RoutingId rid)
        {
            var result = await _locations.RemoveDescriptorAsync(
                new ZLinkMeshNodeDescriptorKey(MeshName, rid));
            Assert.Equal(ZLinkLocationWriteStatus.Stored, result.Status);
        }

        public async ValueTask DisposeAsync()
        {
            try
            {
                await _hosted.StopAsync(CancellationToken.None);
            }
            finally
            {
                await _provider.DisposeAsync();
            }
        }
    }

    private sealed record RouteProbe(string Value);
}
