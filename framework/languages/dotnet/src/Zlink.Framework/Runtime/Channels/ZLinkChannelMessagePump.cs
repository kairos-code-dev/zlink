using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelMessagePump(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher)
{
    public async Task RunServerLoopAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            Received? received = null;
            try
            {
                received = router.Recv(RecvFlags.DontWait);
                if (received is null)
                {
                    await Task.Delay(1, cancellationToken).ConfigureAwait(false);
                    continue;
                }

                await DispatchServerMessageAsync(channelName, router, received, cancellationToken)
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
        while (!cancellationToken.IsCancellationRequested)
        {
            TopicMessage? topicMessage = null;
            try
            {
                topicMessage = subscriber.Subscribe(RecvFlags.DontWait);
                if (topicMessage is null)
                {
                    await Task.Delay(1, cancellationToken).ConfigureAwait(false);
                    continue;
                }

                await DispatchEventMessageAsync(channelName, topicMessage, cancellationToken)
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

    private async Task DispatchServerMessageAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        CancellationToken cancellationToken)
    {
        if (received.Parts.Count == 0)
        {
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts[0]);

        switch (header.Kind)
        {
            case ZLinkMessageKind.Request:
                await HandleRequestAsync(channelName, router, received, header, cancellationToken)
                    .ConfigureAwait(false);
                break;
            case ZLinkMessageKind.Command:
                await HandleCommandAsync(channelName, received.Parts[0], header, cancellationToken)
                    .ConfigureAwait(false);
                break;
        }
    }

    private async Task HandleRequestAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var endpoint = handlerRegistry.GetRequest(header.MessageName);
        var message = ZLinkEnvelopeCodec.DecodeBody(received.Parts[0], endpoint.MessageType);
        var context = new ZLinkRequestContext(
            channelName,
            header.MessageName,
            header.ContentType,
            header.CorrelationId,
            header.Deadline,
            EmptyServiceProvider.Instance,
            cancellationToken);

        try
        {
            var reply = await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                .ConfigureAwait(false);
            var replyHeader = new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Response,
                channelName,
                header.MessageName,
                ZLinkEnvelopeCodec.DefaultContentType,
                header.CorrelationId,
                null,
                null,
                null,
                null);
            using var replyMessage = ZLinkEnvelopeCodec.Encode(replyHeader, reply, endpoint.ReplyType);
            var routingId = received.RoutingId
                ?? throw new InvalidOperationException("Request reply requires a routing id.");
            router.Reply(routingId, received.RequestSeq ?? 0UL, replyMessage);
        }
        catch (Exception ex)
        {
            var errorHeader = new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Error,
                channelName,
                header.MessageName,
                ZLinkEnvelopeCodec.DefaultContentType,
                header.CorrelationId,
                null,
                null,
                ex.GetType().Name,
                ex.Message);
            using var replyMessage = ZLinkEnvelopeCodec.Encode(errorHeader, null, null);
            var routingId = received.RoutingId
                ?? throw new InvalidOperationException("Error reply requires a routing id.");
            router.Reply(routingId, received.RequestSeq ?? 0UL, replyMessage);
        }
    }

    private async Task HandleCommandAsync(
        string channelName,
        Message envelope,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var endpoint = handlerRegistry.GetCommand(header.MessageName);
        var message = ZLinkEnvelopeCodec.DecodeBody(envelope, endpoint.MessageType);
        var context = new ZLinkSendContext(
            channelName,
            header.MessageName,
            header.ContentType,
            header.CorrelationId,
            header.Deadline,
            EmptyServiceProvider.Instance,
            cancellationToken);
        await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
            .ConfigureAwait(false);
    }

    private async Task DispatchEventMessageAsync(
        string channelName,
        TopicMessage topicMessage,
        CancellationToken cancellationToken)
    {
        if (topicMessage.Parts.Count == 0)
        {
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(topicMessage.Parts[0]);
        var endpoints = handlerRegistry.GetEvents(header.MessageName);

        foreach (var endpoint in endpoints)
        {
            var message = ZLinkEnvelopeCodec.DecodeBody(topicMessage.Parts[0], endpoint.MessageType);
            var context = new ZLinkEventContext(
                channelName,
                header.MessageName,
                header.ContentType,
                header.CorrelationId,
                header.Deadline,
                topicMessage.Topic,
                header.Source,
                EmptyServiceProvider.Instance,
                cancellationToken);
            await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
