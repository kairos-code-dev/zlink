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
            await channels.InitializeInboundChannelsAsync(state, channelAdapter).ConfigureAwait(false);
            await channels.InitializePublisherChannelsAsync(state, channelAdapter).ConfigureAwait(false);
            await channels.InitializeClientChannelsAsync(state).ConfigureAwait(false);
            await spots.InitializeSpotNodesAsync(state).ConfigureAwait(false);
            await channels.InitializeRouteChannelsAsync(state, channelAdapter).ConfigureAwait(false);
            await streams.InitializeStreamNodesAsync(state).ConfigureAwait(false);
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
