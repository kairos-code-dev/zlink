using Zlink.Framework.Runtime.Backend.Contracts;

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
            channels.InitializeRouteChannels(state, channelAdapter);
            await spots.InitializeSpotNodesAsync(state).ConfigureAwait(false);
            streams.InitializeStreamNodes(state);
            return state;
        }
        catch (Exception error)
        {
            if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_FRAMEWORK_STARTUP") == "1")
            {
                Console.Error.WriteLine(error);
            }
            await state.DisposeAsync();
            throw;
        }
    }
}
