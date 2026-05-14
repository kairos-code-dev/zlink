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
            var replyParts = ZLinkEnvelopeCodec.EncodeParts(replyHeader, reply, endpoint.ReplyType);
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
            var replyParts = ZLinkEnvelopeCodec.EncodeParts(errorHeader, null, null);
            var routingId = received.RoutingId
                ?? throw new InvalidOperationException("Error reply requires a routing id.");
            try
            {
                router.Reply(routingId, received.RequestSeq ?? 0UL, replyParts);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(replyParts);
            }
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

    private static readonly IReadOnlySet<string> EmptyGroups = new HashSet<string>(StringComparer.Ordinal);
}
