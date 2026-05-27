using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Registry;

public sealed class RegistryContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkRegistryQuery),
        typeof(IZLinkRegistryQueryClient),
        typeof(IZLinkRegistryQueryClientOptions),
        typeof(IZLinkRegistryOptions))]
    public async Task Registry_contracts_describe_status_topology_and_client_options()
    {
        var options = new ExampleRegistryOptions();
        options.PubEndpoint = "tcp://127.0.0.1:6001";
        options.RouterEndpoint = "tcp://127.0.0.1:6002";
        options.AddPeer("tcp://127.0.0.1:7001");

        var clientOptions = new ExampleRegistryQueryClientOptions
        {
            Endpoint = options.RouterEndpoint
        };

        var query = new ExampleRegistryQuery();
        var status = await query.StatusSnapshotAsync();
        var topology = await query.TopologyQueryAsync(new ZLinkRegistryTopologyFilter(ChannelName: "play"));
        IZLinkRegistryQueryClient client = query;
        var snapshot = await client.SnapshotAsync();

        Assert.Equal("tcp://127.0.0.1:6002", clientOptions.Endpoint);
        Assert.Equal(ZLinkRegistryState.Active, status.State);
        Assert.Single(topology);
        Assert.Single(snapshot);
        Assert.Equal(["tcp://127.0.0.1:7001"], options.Peers);
    }

    private sealed class ExampleRegistryQuery : IZLinkRegistryQuery, IZLinkRegistryQueryClient
    {
        private readonly ZLinkRegistryTopologyEntry[] _topology =
        [
            new(
                ZLinkAutoConnectType.RouteMesh,
                RoutingId.From("play-node"),
                ZLinkServiceKind.Socket,
                ZLinkServiceRole.Router,
                "play",
                "tcp://127.0.0.1:5001",
                ZLinkTopologySource.Manual,
                ZLinkTopologyState.Ready,
                1,
                1,
                0,
                1,
                ZLinkSpotKind.Invalid)
        ];

        public ValueTask<ZLinkRegistryStatus> StatusSnapshotAsync(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkRegistryStatus(
                1,
                "tcp://127.0.0.1:6001",
                ZLinkRegistryState.Active,
                1,
                0,
                0,
                1,
                0,
                1));

        public ValueTask<ZLinkRegistryServiceSummaryEntry[]> ServiceSummarySnapshotAsync(
            ZLinkRegistryServiceSummaryFilter? filter = null,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(Array.Empty<ZLinkRegistryServiceSummaryEntry>());

        public ValueTask<ZLinkRegistryTopologyEntry[]> TopologySnapshotAsync(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_topology);

        public ValueTask<ZLinkRegistryTopologyEntry[]> TopologyQueryAsync(
            ZLinkRegistryTopologyFilter? filter = null,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_topology
                .Where(entry => filter?.ChannelName is null || entry.ChannelName == filter.ChannelName)
                .ToArray());

        public ValueTask<ZLinkMemberPeerEntry[]> MemberPeersAsync(
            string channelName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(Array.Empty<ZLinkMemberPeerEntry>());

        public ValueTask<ZLinkRegistryTopologyEntry[]> SnapshotAsync(
            ZLinkRegistryTopologyFilter? filter = null,
            CancellationToken cancellationToken = default) =>
            TopologyQueryAsync(filter, cancellationToken);
    }

    private sealed class ExampleRegistryQueryClientOptions : IZLinkRegistryQueryClientOptions
    {
        public string Endpoint { get; set; } = "";
    }

    private sealed class ExampleRegistryOptions : IZLinkRegistryOptions
    {
        private readonly List<string> _peers = [];

        public string PubEndpoint { get; set; } = "";

        public string RouterEndpoint { get; set; } = "";

        public uint RegistryId { get; set; }

        public TimeSpan HeartbeatInterval { get; set; }

        public TimeSpan HeartbeatTimeout { get; set; }

        public TimeSpan BroadcastInterval { get; set; }

        public IReadOnlyList<string> Peers => _peers;

        public void AddPeer(string peerPubEndpoint) => _peers.Add(peerPubEndpoint);
    }
}
