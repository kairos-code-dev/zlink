using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelReceiveLoop(ZLinkChannelPacketDispatcher dispatcher)
{
    public async Task RunServerLoopAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        CancellationToken cancellationToken)
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
            }
        }
    }

    public async Task RunSubscriberLoopAsync(
        string channelName,
        IZLinkBackendSubscriberSocket subscriber,
        CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        while (!cancellationToken.IsCancellationRequested)
        {
            TopicMessage? topicMessage = null;
            try
            {
                topicMessage = subscriber.Subscribe(RecvFlags.DontWait);
                if (topicMessage is null)
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
            finally
            {
                topicMessage?.Dispose();
            }
        }
    }
}
