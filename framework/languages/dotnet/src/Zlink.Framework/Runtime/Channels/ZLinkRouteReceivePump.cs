namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteReceivePump(
    IZLinkBackendRouterSocket router,
    Func<IZLinkBackendSpotRouteBridge?> spotRouteBridge,
    string routerChannelId,
    ZLinkRoutePacketDispatcher dispatcher)
{
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        while (!cancellationToken.IsCancellationRequested)
        {
            Received? received = null;
            try
            {
                received = router.Recv(RecvFlags.DontWait);
                if (received is null)
                {
                    await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                    continue;
                }

                backoff.Reset();
                if (TryHandleSpotRouteBridgePacket(received)) continue;

                await dispatcher.DispatchAsync(received, cancellationToken).ConfigureAwait(false);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
            {
                await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (ZlinkRecvException ex)
                when (cancellationToken.IsCancellationRequested
                      || ex.Result == ZlinkRecvException.ErrorCode.InvalidHandle)
            {
                return;
            }
            catch (ZlinkRecvException ex)
                when (ex.Result == ZlinkRecvException.ErrorCode.InternalError)
            {
                await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
            }
            finally
            {
                received?.Dispose();
            }
        }
    }

    private bool TryHandleSpotRouteBridgePacket(Received received)
    {
        var bridge = spotRouteBridge();
        if (bridge is null
            || received.RoutingId is not { } sourceNodeRid)
            return false;

        var handled = received.RequestSeq is { } requestSeq
            ? bridge.HandleRouterReceived(routerChannelId, sourceNodeRid, requestSeq, received.Parts)
            : bridge.HandleRouterReceived(routerChannelId, sourceNodeRid, 0, received.Parts);
        if (handled) bridge.Drain();

        return handled;
    }
}