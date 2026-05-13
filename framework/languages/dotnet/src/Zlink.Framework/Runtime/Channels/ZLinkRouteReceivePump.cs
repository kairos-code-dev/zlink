using Zlink.Framework.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteReceivePump(
    IZLinkBackendRouterSocket router,
    ZLinkRoutePacketDispatcher dispatcher)
{
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            Received? received = null;
            try
            {
                received = router.Recv(RecvFlags.DontWait);
                if (received is null)
                {
                    await ZLinkPollingBackoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                    continue;
                }

                await dispatcher.DispatchAsync(received, cancellationToken).ConfigureAwait(false);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
            {
                await ZLinkPollingBackoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
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
                await ZLinkPollingBackoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
            }
            finally
            {
                received?.Dispose();
            }
        }
    }
}
