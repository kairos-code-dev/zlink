using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Registry;

namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkRuntimeStateFactory(
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkChannelRuntimeManager channels,
    ZLinkStreamRuntimeManager streams,
    ZLinkSpotRuntimeManager spots)
{
    public async ValueTask<ZLinkFrameworkRuntimeState> CreateAsync()
    {
        var channelAdapter = backendAdapterFactory.CreateChannelAdapter();
        var state = new ZLinkFrameworkRuntimeState(channelAdapter.CreateContext(), registration);

        try
        {
            channels.InitializeInboundChannels(state, channelAdapter);
            channels.InitializePublisherChannels(state, channelAdapter);
            channels.InitializeClientChannels(state);
            await spots.InitializeSpotNodesAsync(state).ConfigureAwait(false);
            channels.InitializeRouteChannels(state, channelAdapter);
            InitializeRegistrySpotDiscovery(state, channelAdapter);
            streams.InitializeStreamNodes(state);
            return state;
        }
        catch (Exception error)
        {
            ZLinkFrameworkDebugLog.Startup(error);
            await state.DisposeAsync();
            throw;
        }
    }

    private static void InitializeRegistrySpotDiscovery(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter channelAdapter)
    {
        var registryRoutes = state.Registration.RegistrySpotRemoteAddresses;
        if (registryRoutes is null
            || state.SpotNodes.Count > 0)
        {
            return;
        }

        var endpoints = state.Registration.Discovery?.Endpoints ?? [];
        if (endpoints.Count == 0)
        {
            return;
        }

        var discovery = ZLinkBackendDiscoveryFactory.Create(
            channelAdapter,
            state.Context,
            registryRoutes.Namespace,
            ZLinkAutoConnectType.SpotMesh,
            endpoints);
        discovery.SpotOwnerSyncEnabled = true;
        state.SpotDiscoveries.Add($"{registryRoutes.Namespace}.registry-spot", discovery);
    }
}
