using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelMessagePump
{
    private readonly ZLinkChannelReceiveLoop _receiveLoop;

    public ZLinkChannelMessagePump(
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher,
        ZLinkFrameworkRegistration registration,
        ZLinkFrameworkRuntime runtime,
        ILoggerFactory? loggerFactory = null)
    {
        loggerFactory ??= NullLoggerFactory.Instance;
        _receiveLoop = new ZLinkChannelReceiveLoop(
            new ZLinkChannelPacketDispatcher(
                handlerRegistry,
                dispatcher,
                registration,
                runtime,
                loggerFactory.CreateLogger<ZLinkChannelPacketDispatcher>()));
    }

    public Task RunServerLoopAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        SemaphoreSlim receiveGate,
        Func<IZLinkBackendSpotRouteBridge?> spotRouteBridge,
        CancellationToken cancellationToken)
    {
        return _receiveLoop.RunServerLoopAsync(
            channelName,
            router,
            receiveGate,
            spotRouteBridge,
            cancellationToken);
    }

    public Task RunSubscriberLoopAsync(
        string channelName,
        IZLinkBackendSubscriberSocket subscriber,
        CancellationToken cancellationToken)
    {
        return _receiveLoop.RunSubscriberLoopAsync(channelName, subscriber, cancellationToken);
    }
}