namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelReceiveLoop(
    ZLinkFanoutPacketDispatcher dispatcher,
    ZLinkClientServerDispatcher clientServerDispatcher)
{
    public async Task RunClientServerLoopAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        IZLinkRuntimeErrorSink errorSink,
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
                if (received.RequestSeq is null
                    && received.Parts.Count == 1
                    && received.Parts[0].Size == 0)
                    continue;
                await clientServerDispatcher.DispatchAsync(
                        channelName,
                        router,
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
            catch (Exception exception)
            {
                errorSink.ReportRuntimeTaskException(
                    $"client-server-dispatch:{channelName}",
                    exception);
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
}
