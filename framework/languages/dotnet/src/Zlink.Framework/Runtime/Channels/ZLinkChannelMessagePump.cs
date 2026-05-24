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
        CancellationToken cancellationToken)
    {
        return _receiveLoop.RunServerLoopAsync(channelName, router, receiveGate, cancellationToken);
    }

    public Task RunSubscriberLoopAsync(
        string channelName,
        IZLinkBackendSubscriberSocket subscriber,
        CancellationToken cancellationToken)
    {
        return _receiveLoop.RunSubscriberLoopAsync(channelName, subscriber, cancellationToken);
    }

    public Task RunDealerMeshLoopAsync(
        string channelName,
        IZLinkBackendDealerSocket dealer,
        ZLinkDealerMeshPendingRequests pendingRequests,
        SemaphoreSlim receiveGate,
        CancellationToken cancellationToken)
    {
        return _receiveLoop.RunDealerMeshLoopAsync(
            channelName,
            dealer,
            pendingRequests,
            receiveGate,
            cancellationToken);
    }
}
