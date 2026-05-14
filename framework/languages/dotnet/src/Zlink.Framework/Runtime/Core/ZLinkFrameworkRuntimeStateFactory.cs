using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Core;

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
            channels.InitializeRouteChannels(state, channelAdapter);
            streams.InitializeStreamNodes(state);
            await spots.InitializeSpotNodesAsync(state).ConfigureAwait(false);
            return state;
        }
        catch
        {
            await state.DisposeAsync();
            throw;
        }
    }
}
