namespace Zlink.Framework.Runtime.Registry;

internal sealed class ZLinkRegistryQuery(ZLinkRegistryRuntime runtime) : IZLinkRegistryQuery
{
    public async ValueTask<ZLinkRegistryStatus> StatusAsync(
        CancellationToken cancellationToken = default)
    {
        return await runtime.ExecuteAsync(
            static registry => registry.Status(),
            cancellationToken);
    }

    public async ValueTask<ZLinkRegistryServiceSummaryEntry[]> ServiceSummaryAsync(
        ZLinkRegistryServiceSummaryFilter? filter = null,
        CancellationToken cancellationToken = default)
    {
        return await runtime.ExecuteAsync(
            registry => registry.ServiceSummary(filter).ToArray(),
            cancellationToken);
    }

    public async ValueTask<ZLinkRegistryTopologyEntry[]> TopologyAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default)
    {
        return await runtime.ExecuteAsync(
            registry => registry.Topology(filter).ToArray(),
            cancellationToken);
    }

    public async ValueTask<ZLinkMemberPeerEntry[]> MemberPeersAsync(
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return await runtime.ExecuteAsync(
            registry => registry.MemberPeers(channelName).ToArray(),
            cancellationToken);
    }
}

internal sealed class ZLinkRegistryQueryClientService : IZLinkRegistryQueryClient, IAsyncDisposable
{
    private readonly IZLinkBackendRegistryQueryClient _client;
    private readonly IZLinkBackendContext _context;

    public ZLinkRegistryQueryClientService(
        IZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkRegistryQueryClientRegistration registration)
    {
        var channelAdapter = backendAdapterFactory.CreateChannelAdapter();
        var registryAdapter = backendAdapterFactory.CreateRegistryAdapter();
        _context = channelAdapter.CreateContext();
        _client = registryAdapter.CreateRegistryQueryClient(_context);
        _client.Connect(registration.Endpoint!);
    }

    public async ValueTask DisposeAsync()
    {
        await _client.DisposeAsync();
        await _context.DisposeAsync();
    }

    public ValueTask<ZLinkRegistryTopologyEntry[]> TopologyAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default)
    {
        return ValueTask.FromResult(_client.Topology(filter).ToArray());
    }
}