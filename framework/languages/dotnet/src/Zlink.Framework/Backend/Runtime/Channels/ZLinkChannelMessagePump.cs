namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelMessagePump
{
    private readonly ZLinkChannelReceiveLoop _receiveLoop;

    public ZLinkChannelMessagePump(
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher,
        ZLinkFrameworkRegistration registration)
    {
        _receiveLoop = new ZLinkChannelReceiveLoop(
            new ZLinkChannelPacketDispatcher(handlerRegistry, dispatcher, registration));
    }

    public Task RunServerLoopAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        CancellationToken cancellationToken)
    {
        return _receiveLoop.RunServerLoopAsync(channelName, router, cancellationToken);
    }

    public Task RunSubscriberLoopAsync(
        string channelName,
        IZLinkBackendSubscriberSocket subscriber,
        CancellationToken cancellationToken)
    {
        return _receiveLoop.RunSubscriberLoopAsync(channelName, subscriber, cancellationToken);
    }
}
