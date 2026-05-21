using Zlink.Framework.Runtime.Core;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelPacketDispatcher(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    ZLinkFrameworkRegistration registration,
    ZLinkFrameworkRuntime runtime)
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
                if (header.MessageName == ZLinkRoutedSpotRelayPackets.RequestPacketName)
                {
                    await HandleRoutedSpotRequestAsync(channelName, router, received, header, cancellationToken)
                        .ConfigureAwait(false);
                    return;
                }

                await HandleRequestAsync(channelName, router, received, header, cancellationToken)
                    .ConfigureAwait(false);
                break;
            case ZLinkMessageKind.Command:
                if (header.MessageName == ZLinkRoutedSpotRelayPackets.SendPacketName)
                {
                    await HandleRoutedSpotSendAsync(channelName, router, received, cancellationToken)
                        .ConfigureAwait(false);
                    return;
                }

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

    private async Task HandleRoutedSpotSendAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        CancellationToken cancellationToken)
    {
        var metadata = ZLinkRoutedSpotRelayPackets.DecodeMetadata(received);
        var targetNodeRid = runtime.ResolveAcceptedSpotRouteNodeRid(channelName);
        var targetSpotRid = RoutingId.FromBytes(metadata.TargetSpotRid);
        var spotParts = ZLinkRoutedSpotRelayPackets.CopySpotPayloadParts(received);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!router.SendToSpot(targetNodeRid, targetSpotRid, spotParts, SendFlags.None))
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Routed SPOT ingress channel '{channelName}' is not ready for SPOT send.");
            }
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(spotParts);
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    private async Task HandleRoutedSpotRequestAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Routed SPOT relay request requires a source routing id.");
        try
        {
            var metadata = ZLinkRoutedSpotRelayPackets.DecodeMetadata(received);
            var targetNodeRid = runtime.ResolveAcceptedSpotRouteNodeRid(channelName);
            var targetSpotRid = RoutingId.FromBytes(metadata.TargetSpotRid);
            var timeout = header.Deadline is { } deadline
                ? deadline - DateTimeOffset.UtcNow
                : registration.DefaultTimeout;
            if (timeout <= TimeSpan.Zero)
            {
                timeout = TimeSpan.FromMilliseconds(1);
            }

            var spotParts = ZLinkRoutedSpotRelayPackets.CopySpotPayloadParts(received);
            IReadOnlyList<Message> reply;
            try
            {
                reply = await RequestSpotFromIngressRouterAsync(
                        channelName,
                        router,
                        targetNodeRid,
                        targetSpotRid,
                        spotParts,
                        timeout,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(spotParts);
            }

            ZLinkChannelReplyWriter.ReplyRawParts(
                router,
                sourceRid,
                received.RequestSeq,
                reply);
        }
        catch (Exception ex)
        {
            ZLinkChannelReplyWriter.ReplyEnvelope(
                router,
                sourceRid,
                received.RequestSeq,
                ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, ex),
                null,
                null);
        }
    }

    private static async ValueTask<IReadOnlyList<Message>> RequestSpotFromIngressRouterAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        if (!router.RequestToSpot(
                targetNodeRid,
                targetSpotRid,
                parts,
                (result, reply) => ZLinkRawReplyCompletion.Complete(
                    result,
                    reply,
                    completion,
                    $"Routed SPOT ingress channel '{channelName}' request failed with result '{result}'."),
                SendFlags.None,
                timeout))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Routed SPOT ingress channel '{channelName}' is not ready for SPOT request.");
        }

        using var _ = cancellationToken.Register(
            static state => ((TaskCompletionSource<IReadOnlyList<Message>>)state!)
                .TrySetCanceled(),
            completion);

        return await completion.Task.ConfigureAwait(false);
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
