namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelReceiveLoop(
    ZLinkFanoutPacketDispatcher dispatcher,
    ZLinkClientServerDispatcher clientServerDispatcher)
{
    public async Task RunClientServerLoopAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        ZLinkClientServerServerIdentity identity,
        IZLinkRuntimeFailureReporter errorSink,
        CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        await using var applicationDispatch =
            new ZLinkChannelApplicationDispatchQueue(
                $"client-server-application:{channelName}",
                errorSink,
                cancellationToken);
        identity.AttachRouter(router);
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                Received? received = null;
                try
                {
                    identity.TickLiveness(router);
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
                    if (ZLinkClientServerControlProtocol.IsControl(received.Parts))
                    {
                        ReplyClientServerControl(router, received, identity);
                        continue;
                    }
                    var owned = received;
                    received = null;
                    applicationDispatch.TryPost(
                        token => DispatchClientServerAsync(
                            channelName,
                            router,
                            owned,
                            applicationDispatch.ReplyGate,
                            token),
                        () => RejectClientServerDispatch(
                            channelName,
                            router,
                            owned,
                            applicationDispatch.ReplyGate));
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
        finally
        {
            identity.DetachRouter(router);
        }
    }

    private void RejectClientServerDispatch(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkChannelReplyGate replyGate)
    {
        using (received)
            clientServerDispatcher.RejectOverloaded(
                channelName,
                router,
                received,
                replyGate);
    }

    private async ValueTask DispatchClientServerAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkChannelReplyGate replyGate,
        CancellationToken cancellationToken)
    {
        using (received)
            await clientServerDispatcher.DispatchAsync(
                    channelName,
                    router,
                    received,
                    replyGate,
                    cancellationToken)
                .ConfigureAwait(false);
    }

    private static void ReplyClientServerControl(
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkClientServerServerIdentity identity)
    {
        if (received.RoutingId is not { } sourceRid)
            return;
        if (ZLinkClientServerControlProtocol.TryDecodeLivenessAck(
                received.Parts,
                out var ackId))
        {
            identity.AcceptLivenessAck(sourceRid, ackId);
            return;
        }
        if (ZLinkClientServerControlProtocol.TryDecodeLivenessProbe(
                received.Parts,
                out var probeId))
        {
            identity.RecordLivenessProbe(sourceRid);
            var ack =
                ZLinkClientServerControlProtocol.EncodeLivenessAck(probeId);
            if (received.RequestSeq is not null)
                ReplyOwned(router, sourceRid, received.RequestSeq, ack);
            else
                SendOwned(router, sourceRid, ack);
            return;
        }
        var snapshot = identity.Read();
        var valid = ZLinkClientServerControlProtocol.TryDecodeHello(
            received.Parts,
            out var hello);
        var accepted = valid
            && hello is not null
            && StringComparer.Ordinal.Equals(
                hello.ChannelName,
                identity.ChannelName)
            && StringComparer.Ordinal.Equals(
                hello.SecurityIdentity,
                identity.SecurityIdentity);
        var reply = accepted
            ? ZLinkClientServerControlProtocol.EncodeAdmission(
                identity.ToAdmission(snapshot) with
                {
                    NormalizedEffectiveMaxMessageBytes = Math.Min(
                        hello!.NormalizedEffectiveMaxMessageBytes,
                        identity.NormalizedEffectiveMaxMessageBytes)
                })
            : ZLinkClientServerControlProtocol.EncodeReject(reason: 1);
        if (ReplyOwned(router, sourceRid, received.RequestSeq, reply)
            && accepted)
            identity.AdmitPeer(sourceRid);
    }

    private static bool ReplyOwned(
        IZLinkBackendRouterSocket router,
        RoutingId sourceRid,
        ulong? requestSeq,
        Message reply)
    {
        if (requestSeq is not { } value)
        {
            reply.Dispose();
            return false;
        }
        try
        {
            router.Reply(sourceRid, value, reply);
            return true;
        }
        catch
        {
            reply.Dispose();
            throw;
        }
    }

    private static bool SendOwned(
        IZLinkBackendRouterSocket router,
        RoutingId sourceRid,
        Message message)
    {
        try
        {
            if (router.Send(sourceRid, message, SendFlags.DontWait))
                return true;
        }
        catch
        {
        }
        message.Dispose();
        return false;
    }

    public async Task RunSubscriberLoopAsync(
        string channelName,
        IZLinkBackendSubscriberSocket subscriber,
        IZLinkRuntimeFailureReporter errorSink,
        CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        await using var applicationDispatch =
            new ZLinkChannelApplicationDispatchQueue(
                $"fanout-application:{channelName}",
                errorSink,
                cancellationToken);
        while (!cancellationToken.IsCancellationRequested)
        {
            TopicMessage? topicMessage = new();
            try
            {
                if (!subscriber.Subscribe(topicMessage, RecvFlags.DontWait))
                {
                    await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                    continue;
                }

                backoff.Reset();
                var owned = topicMessage;
                topicMessage = null;
                applicationDispatch.TryPost(
                    token => DispatchFanoutAsync(channelName, owned, token),
                    owned.Dispose);
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

    public async Task RunFanoutConnectionLoopAsync(
        string channelName,
        IZLinkBackendSubscriberSocket subscriber,
        Action onActivity,
        Action onProtocolError,
        IZLinkRuntimeFailureReporter errorSink,
        CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        await using var applicationDispatch =
            new ZLinkChannelApplicationDispatchQueue(
                $"fanout-automatic-application:{channelName}",
                errorSink,
                cancellationToken);
        while (!cancellationToken.IsCancellationRequested)
        {
            TopicMessage? topicMessage = new();
            try
            {
                if (!subscriber.Subscribe(topicMessage, RecvFlags.DontWait))
                {
                    await backoff.NoDataAsync(cancellationToken)
                        .ConfigureAwait(false);
                    continue;
                }

                backoff.Reset();
                if (ZLinkFanoutLivenessProtocol.IsReservedTopic(
                        topicMessage.Topic))
                {
                    if (ZLinkFanoutLivenessProtocol.IsValidBeacon(topicMessage))
                    {
                        onActivity();
                        continue;
                    }

                    onProtocolError();
                    return;
                }

                onActivity();
                var owned = topicMessage;
                topicMessage = null;
                applicationDispatch.TryPost(
                    token => DispatchFanoutAsync(channelName, owned, token),
                    owned.Dispose);
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

    private async ValueTask DispatchFanoutAsync(
        string channelName,
        TopicMessage topicMessage,
        CancellationToken cancellationToken)
    {
        using (topicMessage)
            await dispatcher.DispatchEventMessageAsync(
                    channelName,
                    topicMessage,
                    cancellationToken)
                .ConfigureAwait(false);
    }
}
