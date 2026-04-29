using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Registry;

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
                registry.SetId(_registration.RegistryId);
                registry.SetHeartbeat(
                    checked((uint)_registration.HeartbeatInterval.TotalMilliseconds),
                    checked((uint)_registration.HeartbeatTimeout.TotalMilliseconds));
                registry.SetBroadcastInterval(
                    checked((uint)_registration.BroadcastInterval.TotalMilliseconds));

                foreach (var peerEndpoint in _registration.PeerPubEndpoints)
                {
                    registry.AddPeer(peerEndpoint);
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

    public async ValueTask<T> ExecuteAsync<T>(
        Func<IZLinkBackendRegistry, T> action,
        CancellationToken cancellationToken)
    {
        if (_registry is null)
        {
            await StartAsync(cancellationToken);
        }

        await _gate.WaitAsync(cancellationToken);
        try
        {
            return action(_registry
                ?? throw new InvalidOperationException("Embedded registry runtime is not started."));
        }
        finally
        {
            _gate.Release();
        }
    }
}
