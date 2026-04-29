using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Registry;

internal sealed class ZLinkRegistryQuery(ZLinkRegistryRuntime runtime) : IZLinkRegistryQuery
{
    public async ValueTask<ZLinkRegistryStatus> StatusSnapshotAsync(
        CancellationToken cancellationToken = default)
    {
        return await runtime.ExecuteAsync(
            static registry => registry.StatusSnapshot(),
            cancellationToken);
    }

    public async ValueTask<ZLinkRegistryServiceSummaryEntry[]> ServiceSummarySnapshotAsync(
        ZLinkRegistryServiceSummaryFilter? filter = null,
        CancellationToken cancellationToken = default)
    {
        return await runtime.ExecuteAsync(
            registry => registry.ServiceSummarySnapshot(filter).ToArray(),
            cancellationToken);
    }

    public async ValueTask<ZLinkRegistryTopologyEntry[]> TopologySnapshotAsync(
        CancellationToken cancellationToken = default)
    {
        return await runtime.ExecuteAsync(
            static registry => registry.TopologySnapshot().ToArray(),
            cancellationToken);
    }

    public async ValueTask<ZLinkRegistryTopologyEntry[]> TopologyQueryAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default)
    {
        return await runtime.ExecuteAsync(
            registry => registry.TopologyQuery(filter).ToArray(),
            cancellationToken);
    }

    public async ValueTask<ZLinkMemberPeerEntry[]> MemberPeersAsync(
        ZLinkServiceType serviceType,
        string serviceName,
        CancellationToken cancellationToken = default)
    {
        return await runtime.ExecuteAsync(
            registry => registry.MemberPeers(serviceType, serviceName).ToArray(),
            cancellationToken);
    }
}

internal sealed class ZLinkRegistryQueryClientService : IZLinkRegistryQueryClient, IAsyncDisposable
{
    private readonly IZLinkBackendContext _context;
    private readonly IZLinkBackendRegistryQueryClient _client;

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

    public ValueTask<ZLinkRegistryTopologyEntry[]> SnapshotAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default)
    {
        return ValueTask.FromResult(_client.Snapshot(filter).ToArray());
    }

    public async ValueTask DisposeAsync()
    {
        await _client.DisposeAsync();
        await _context.DisposeAsync();
    }
}
