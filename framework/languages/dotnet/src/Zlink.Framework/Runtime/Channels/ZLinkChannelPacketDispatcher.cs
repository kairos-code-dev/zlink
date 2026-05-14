using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelPacketDispatcher(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    ZLinkFrameworkRegistration registration)
{
    private static readonly IReadOnlySet<string> EmptyGroups = new HashSet<string>(StringComparer.Ordinal);

    public async Task DispatchServerMessageAsync(
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

    public async Task DispatchEventMessageAsync(
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
            channelName,
            ResolveMappedGroups(channelName),
            header.MessageName);
        Dictionary<Type, object?>? decodedMessages = null;

        foreach (var endpoint in endpoints)
        {
            decodedMessages ??= new Dictionary<Type, object?>();
            if (!decodedMessages.TryGetValue(endpoint.MessageType, out var message))
            {
                message = ZLinkEnvelopeCodec.DecodeBody(topicMessage.Parts, endpoint.MessageType);
                decodedMessages.Add(endpoint.MessageType, message);
            }

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
            ZLinkChannelReplyWriter.ReplyRequest(
                router,
                received,
                ZLinkChannelReplyWriter.CreateReplyHeader(ZLinkMessageKind.Response, channelName, header),
                reply,
                endpoint.ReplyType);
        }
        catch (Exception ex)
        {
            ZLinkChannelReplyWriter.ReplyRequest(
                router,
                received,
                ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, ex),
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

    private IReadOnlySet<string> ResolveMappedGroups(string channelName)
    {
        return registration.Channels.TryGetValue(channelName, out var channel)
            ? channel.HandlerGroups
            : EmptyGroups;
    }
}
