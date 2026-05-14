namespace Zlink.Framework.Runtime.Registry;

internal sealed class ZLinkRegistryRuntime(
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkRegistryRegistration registration)
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private IZLinkBackendContext? _context;
    private IZLinkBackendRegistry? _registry;

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

            var registryAdapter = backendAdapterFactory.CreateRegistryAdapter();
            var channelAdapter = backendAdapterFactory.CreateChannelAdapter();
            var context = channelAdapter.CreateContext();
            var registry = registryAdapter.CreateRegistry(context);

            try
            {
                if (registration.RegistryId != 0)
                {
                    registry.SetId(registration.RegistryId);
                }
                registry.SetHeartbeat(
                    checked((uint)registration.HeartbeatInterval.TotalMilliseconds),
                    checked((uint)registration.HeartbeatTimeout.TotalMilliseconds));
                registry.SetBroadcastInterval(
                    checked((uint)registration.BroadcastInterval.TotalMilliseconds));

                foreach (var peerEndpoint in registration.PeerPubEndpoints)
                {
                    registry.AddPeer(peerEndpoint);
                }

                registry.Bind(registration.PubEndpoint!, registration.RouterEndpoint!);
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
