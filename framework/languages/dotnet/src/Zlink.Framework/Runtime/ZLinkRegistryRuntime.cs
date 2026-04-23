using Zlink.Framework.Backend;

namespace Zlink.Framework;

internal sealed class ZLinkRegistryRuntime
{
    private readonly IZLinkBackendAdapterFactory _backendAdapterFactory;
    private readonly ZLinkRegistryRegistration _registration;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private IZLinkBackendContext? _context;
    private IZLinkBackendRegistry? _registry;

    public ZLinkRegistryRuntime(
        IZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkRegistryRegistration registration)
    {
        _backendAdapterFactory = backendAdapterFactory;
        _registration = registration;
    }

    public IZLinkBackendContext? Context => _context;

    public IZLinkBackendRegistry? Registry => _registry;

    public bool IsStarted => _registry is not null;

    public async ValueTask StartAsync(CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (_registry is not null)
            {
                return;
            }

            var registryAdapter = _backendAdapterFactory.CreateRegistryAdapter();
            var channelAdapter = _backendAdapterFactory.CreateChannelAdapter();
            var context = channelAdapter.CreateContext();
            var registry = registryAdapter.CreateRegistry(context);

            try
            {
                var nativeRegistry = registry.RequireNative<global::Zlink.Registry>();
                nativeRegistry.SetId(_registration.RegistryId);
                nativeRegistry.SetHeartbeat(
                    checked((uint)_registration.HeartbeatInterval.TotalMilliseconds),
                    checked((uint)_registration.HeartbeatTimeout.TotalMilliseconds));
                nativeRegistry.SetBroadcastInterval(
                    checked((uint)_registration.BroadcastInterval.TotalMilliseconds));

                foreach (var peerEndpoint in _registration.PeerPubEndpoints)
                {
                    nativeRegistry.AddPeer(peerEndpoint);
                }

                registry.Bind(_registration.PubEndpoint!, _registration.RouterEndpoint!);
            }
            catch
            {
                await registry.DisposeAsync();
                await context.DisposeAsync();
                throw;
            }

            _context = context;
            _registry = registry;
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask StopAsync(CancellationToken cancellationToken)
    {
        IZLinkBackendRegistry? registryToDispose;
        IZLinkBackendContext? contextToDispose;

        await _gate.WaitAsync(cancellationToken);
        try
        {
            registryToDispose = _registry;
            contextToDispose = _context;
            _registry = null;
            _context = null;
        }
        finally
        {
            _gate.Release();
        }

        if (registryToDispose is not null)
        {
            await registryToDispose.DisposeAsync();
        }

        if (contextToDispose is not null)
        {
            await contextToDispose.DisposeAsync();
        }
    }
}
