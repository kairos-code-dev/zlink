using Zlink.Framework.Backend;

namespace Zlink.Framework;

internal sealed class ZLinkRegistryQuery(ZLinkRegistryRuntime runtime) : IZLinkRegistryQuery
{
    public ZLinkRegistryStatus StatusSnapshot()
    {
        return RequireRegistry().StatusSnapshot().ToFramework();
    }

    public ZLinkRegistryServiceSummaryEntry[] ServiceSummarySnapshot(
        ZLinkRegistryServiceSummaryFilter? filter = null)
    {
        return RequireRegistry().ServiceSummarySnapshot(filter.ToBackend())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public ZLinkRegistryTopologyEntry[] TopologySnapshot()
    {
        return RequireRegistry().TopologySnapshot()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public ZLinkRegistryTopologyEntry[] TopologyQuery(
        ZLinkRegistryTopologyFilter? filter = null)
    {
        return RequireRegistry().TopologyQuery(filter.ToBackend())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public ZLinkMemberPeerEntry[] MemberPeers(
        ZLinkServiceType serviceType,
        string serviceName)
    {
        return RequireRegistry().MemberPeers((global::Zlink.ServiceType)serviceType, serviceName)
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    private global::Zlink.Registry RequireRegistry()
    {
        if (!runtime.IsStarted)
        {
            runtime.StartAsync(CancellationToken.None).AsTask().GetAwaiter().GetResult();
        }

        return runtime.Registry?.RequireNative<global::Zlink.Registry>()
            ?? throw new InvalidOperationException("Embedded registry runtime is not started.");
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

    public ZLinkRegistryTopologyEntry[] Snapshot(
        ZLinkRegistryTopologyFilter? filter = null)
    {
        return _client.RequireNative<global::Zlink.RegistryQueryClient>().Snapshot(filter.ToBackend())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public async ValueTask DisposeAsync()
    {
        await _client.DisposeAsync();
        await _context.DisposeAsync();
    }
}
