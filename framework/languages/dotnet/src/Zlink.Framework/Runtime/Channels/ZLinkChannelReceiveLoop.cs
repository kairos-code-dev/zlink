using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelReceiveLoop(ZLinkChannelPacketDispatcher dispatcher)
{
    public async Task RunServerLoopAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        SemaphoreSlim receiveGate,
        Func<IZLinkBackendSpotRouteBridge?> spotRouteBridge,
        CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        while (!cancellationToken.IsCancellationRequested)
        {
            Received? received = null;
            var gateHeld = false;
            try
            {
                await receiveGate.WaitAsync(cancellationToken).ConfigureAwait(false);
                gateHeld = true;
                received = router.Recv(RecvFlags.DontWait);
                if (received is null)
                {
                    await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                    continue;
                }

                backoff.Reset();
                if (TryHandleSpotRouteBridgePacket(
                        channelName,
                        spotRouteBridge(),
                        received))
                {
                    continue;
                }

                await dispatcher.DispatchServerMessageAsync(channelName, router, received, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            finally
            {
                received?.Dispose();
                if (gateHeld)
                {
                    receiveGate.Release();
                }
            }
        }
    }

    private static bool TryHandleSpotRouteBridgePacket(
        string channelName,
        IZLinkBackendSpotRouteBridge? bridge,
        Received received)
    {
        if (bridge is null
            || received.RoutingId is not { } sourceNodeRid)
        {
            return false;
        }

        var handled = received.RequestSeq is { } requestSeq
            ? bridge.HandleRouterReceived(channelName, sourceNodeRid, requestSeq, received.Parts)
            : bridge.HandleRouterReceived(channelName, sourceNodeRid, 0, received.Parts);
        if (handled)
        {
            bridge.Drain();
        }

        return handled;
    }

    public async Task RunSubscriberLoopAsync(
        string channelName,
        IZLinkBackendSubscriberSocket subscriber,
        CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        while (!cancellationToken.IsCancellationRequested)
        {
            using var topicMessage = new TopicMessage();
            try
            {
                if (!subscriber.Subscribe(topicMessage, RecvFlags.DontWait))
                {
                    await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                    continue;
                }

                backoff.Reset();
                await dispatcher.DispatchEventMessageAsync(channelName, topicMessage, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
        }
    }

    public async Task RunDealerMeshLoopAsync(
        string channelName,
        IZLinkBackendDealerSocket dealer,
        ZLinkDealerMeshPendingRequests pendingRequests,
        SemaphoreSlim receiveGate,
        CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        while (!cancellationToken.IsCancellationRequested)
        {
            Received? received = null;
            var gateHeld = false;
            try
            {
                await receiveGate.WaitAsync(cancellationToken).ConfigureAwait(false);
                gateHeld = true;
                received = dealer.Recv(RecvFlags.DontWait);
                if (received is null)
                {
                    await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                    continue;
                }

                backoff.Reset();
                await dispatcher.DispatchDealerMeshMessageAsync(
                        channelName,
                        dealer,
                        pendingRequests,
                        received,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            finally
            {
                received?.Dispose();

                if (gateHeld)
                {
                    receiveGate.Release();
                }
            }
        }
    }
}
