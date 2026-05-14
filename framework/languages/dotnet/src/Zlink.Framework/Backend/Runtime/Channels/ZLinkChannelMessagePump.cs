using Zlink.Framework.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelMessagePump(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    ZLinkFrameworkRegistration registration)
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

        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);

        switch (header.Kind)
        {
            case ZLinkMessageKind.Request:
                await HandleRequestAsync(channelName, router, received, header, cancellationToken)
                    .ConfigureAwait(false);
                break;
            case ZLinkMessageKind.Command:
                await HandleCommandAsync(channelName, received.Parts, header, cancellationToken)
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
        var endpoint = handlerRegistry.GetRequest(
            channelName,
            ResolveMappedGroups(channelName),
            header.MessageName);
        var message = ZLinkEnvelopeCodec.DecodeBody(received.Parts, endpoint.MessageType);
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
            ReplyRequest(
                router,
                received,
                CreateReplyHeader(ZLinkMessageKind.Response, channelName, header),
                reply,
                endpoint.ReplyType);
        }
        catch (Exception ex)
        {
            ReplyRequest(
                router,
                received,
                CreateErrorHeader(channelName, header, ex),
                null,
                null);
        }
    }

    private async Task HandleCommandAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var endpoint = handlerRegistry.GetCommand(
            channelName,
            ResolveMappedGroups(channelName),
            header.MessageName);
        var message = ZLinkEnvelopeCodec.DecodeBody(parts, endpoint.MessageType);
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

        var header = ZLinkEnvelopeCodec.DecodeHeader(topicMessage.Parts);
        var endpoints = handlerRegistry.GetPublishes(
            ResolveMappedGroups(channelName),
            header.MessageName);

        foreach (var endpoint in endpoints)
        {
            var message = ZLinkEnvelopeCodec.DecodeBody(topicMessage.Parts, endpoint.MessageType);
            var context = new ZLinkPublishContext(
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

    private IReadOnlySet<string> ResolveMappedGroups(string channelName)
    {
        return registration.Channels.TryGetValue(channelName, out var channel)
            ? channel.HandlerGroups
            : EmptyGroups;
    }

    private static void ReplyRequest(
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkEnvelopeHeader header,
        object? body,
        Type? bodyType)
    {
        var replyParts = ZLinkEnvelopeCodec.EncodeParts(header, body, bodyType);
        var routingId = received.RoutingId
            ?? throw new InvalidOperationException("Request reply requires a routing id.");
        try
        {
            router.Reply(routingId, received.RequestSeq ?? 0UL, replyParts);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }

    private static ZLinkEnvelopeHeader CreateReplyHeader(
        ZLinkMessageKind kind,
        string channelName,
        ZLinkEnvelopeHeader request)
    {
        return new ZLinkEnvelopeHeader(
            kind,
            channelName,
            request.MessageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            request.CorrelationId,
            null,
            null,
            null,
            null);
    }

    private static ZLinkEnvelopeHeader CreateErrorHeader(
        string channelName,
        ZLinkEnvelopeHeader request,
        Exception exception)
    {
        return new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Error,
            channelName,
            request.MessageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            request.CorrelationId,
            null,
            null,
            exception.GetType().Name,
            exception.Message);
    }

    private static readonly IReadOnlySet<string> EmptyGroups = new HashSet<string>(StringComparer.Ordinal);
}
