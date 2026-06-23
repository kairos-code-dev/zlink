namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteBridgeProvider(
    ZLinkFrameworkRegistration registration,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<string, ZLinkChannelRuntimeBundle> getClientBundle,
    Func<string, ZLinkRouteChannelRuntime> getRouteChannel)
{
    public IZLinkBackendSpotRouteBridge GetOrCreateClientBridge(string localEgressChannelName)
    {
        var state = getState();
        var bundle = getClientBundle(localEgressChannelName);
        if (bundle.SpotRouteBridge is { } existing)
        {
            return existing;
        }

        lock (state.SyncRoot)
        {
            if (bundle.SpotRouteBridge is { } lockedExisting)
            {
                return lockedExisting;
            }

            var owner = ResolveOwner(state);
            var bridge = owner.Node.CreateRouteBridge();
            try
            {
                bridge.AttachDealerChannel(
                    localEgressChannelName,
                    (IZLinkBackendDealerSocket)bundle.Socket,
                    new SpotRouteBridgeEndpointOptions
                    {
                        Capabilities = SpotRouteBridgeEndpointCapabilities.RouteOnly
                    });
                state.SpotRouteBridges.Add(bridge);
            }
            catch
            {
                _ = bridge.DisposeAsync();
                throw;
            }

            bundle.SpotRouteBridge = bridge;
            return bridge;
        }
    }

    public ZLinkRouteChannelRuntime GetRouteChannelWithBridge(string channelName)
    {
        var routeChannel = getRouteChannel(channelName);
        if (routeChannel.HasSpotRouteBridge)
        {
            return routeChannel;
        }

        var state = getState();
        lock (state.SyncRoot)
        {
            if (routeChannel.HasSpotRouteBridge)
            {
                return routeChannel;
            }

            var owner = ResolveOwner(state);
            var bridge = owner.Node.CreateRouteBridge();
            try
            {
                routeChannel.AttachSpotRouteBridge(bridge);
                state.SpotRouteBridges.Add(bridge);
            }
            catch
            {
                _ = bridge.DisposeAsync();
                throw;
            }
        }

        return routeChannel;
    }

    public ZLinkSpotRouteEgressResolver CreateEgressResolver()
    {
        return new ZLinkSpotRouteEgressResolver(
            registration,
            backendAdapterFactory,
            getState,
            getRouteChannel);
    }

    private ZLinkSpotNodeRuntime ResolveOwner(ZLinkFrameworkRuntimeState state)
    {
        foreach (var spotNode in registration.SpotNodes.Values)
        {
            if (spotNode.Router is null)
            {
                continue;
            }

            if (state.SpotNodes.TryGetValue(spotNode.SpotNodeName, out var runtime))
            {
                return runtime;
            }
        }

        throw new ZLinkConfigurationException(
            "Routed SPOT egress through a client/server channel requires a router-capable SPOT node in this process.");
    }
}
