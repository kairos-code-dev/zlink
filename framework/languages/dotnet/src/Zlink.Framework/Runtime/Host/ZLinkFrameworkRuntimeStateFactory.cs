namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkRuntimeStateFactory(
    ZLinkFrameworkRuntime frameworkRuntime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkChannelRuntimeManager channels,
    ZLinkStreamRuntimeManager streams,
    ZLinkSpotRuntimeManager spots)
{
    public async ValueTask<ZLinkFrameworkRuntimeState> CreateAsync()
    {
        await ZLinkSpotStartupValidator.ValidateAsync(
                frameworkRuntime.Services,
                registration)
            .ConfigureAwait(false);

        var channelAdapter = backendAdapterFactory.CreateChannelAdapter();
        IZLinkBackendContext? context = null;
        ZLinkFrameworkRuntimeState? state = null;

        try
        {
            context = channelAdapter.CreateContext();
            state = new ZLinkFrameworkRuntimeState(
                context,
                registration,
                frameworkRuntime.Services,
                frameworkRuntime.PrepareErrorSink(),
                frameworkRuntime.ExecutionOwner);
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
            var failures = new ZLinkFailureCollector(error);
            if (state is not null)
            {
                await failures.CaptureAsync(state.DisposeAsync).ConfigureAwait(false);
                frameworkRuntime.DetachErrorSink(state.ErrorSink);
            }
            else if (context is not null)
                await failures.CaptureAsync(context.DisposeAsync).ConfigureAwait(false);
            failures.ThrowIfAny();
            throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
        }
    }

}
