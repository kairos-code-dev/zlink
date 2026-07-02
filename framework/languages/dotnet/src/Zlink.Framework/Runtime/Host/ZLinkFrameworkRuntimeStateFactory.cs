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

}