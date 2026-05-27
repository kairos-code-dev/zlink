using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Zlink.Framework.Runtime.Core;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelPacketDispatcher(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    ZLinkFrameworkRegistration registration,
    ZLinkFrameworkRuntime runtime,
    ILogger<ZLinkChannelPacketDispatcher>? logger = null)
{
    private static readonly IReadOnlySet<string> EmptyGroups = new HashSet<string>(StringComparer.Ordinal);
    private readonly ILogger<ZLinkChannelPacketDispatcher> _logger =
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance;

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
        if (endpoints.Count == 0)
        {
            ZLinkMessageFlowLogger.Dropped(
                _logger,
                registration.DispatchOptions.Unhandled.PublishLogLevel,
                "Channel",
                "Publish",
                header.MessageName,
                "no-handler",
                channelName);
            return;
        }

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
                topicMessage.Topic,
                header.Source,
                cancellationToken);
            await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async Task DispatchDealerMeshMessageAsync(
        string channelName,
        IZLinkBackendDealerSocket dealer,
        ZLinkDealerMeshPendingRequests pendingRequests,
        Received received,
        CancellationToken cancellationToken)
    {
        if (received.Parts.Count == 0)
        {
            return;
        }

        switch (received.MessageType)
        {
            case ReceivedMessageType.Reply:
            case ReceivedMessageType.ErrorReply:
                if (!received.RequestSeq.HasValue
                    || !pendingRequests.TryComplete(
                        received.RequestSeq.Value, received.Parts))
                {
                    ZLinkMessageFlowLogger.Dropped(
                        _logger,
                        LogLevel.Warning,
                        "DealerMeshChannel",
                        received.MessageType == ReceivedMessageType.Reply
                            ? "Response"
                            : "Error",
                        "unknown",
                        "unexpected-reply",
                        channelName);
                }
                return;
            case ReceivedMessageType.Request:
                await DispatchDealerMeshRequestAsync(
                        channelName,
                        dealer,
                        received,
                        cancellationToken)
                    .ConfigureAwait(false);
                return;
            case ReceivedMessageType.Raw:
                await DispatchDealerMeshRawAsync(
                        channelName,
                        received,
                        cancellationToken)
                    .ConfigureAwait(false);
                return;
        }
    }

    private async Task HandleRequestAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (!handlerRegistry.TryGetRequest(
                channelName,
                ResolveMappedGroups(channelName),
                header.MessageName,
                out var endpoint)
            || endpoint is null)
        {
            var error = new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.HandlerNotFound,
                $"No request handler is registered for '{channelName}:{header.MessageName}'.");
            ZLinkMessageFlowLogger.HandlerMissing(
                _logger,
                LogLevel.Warning,
                "Channel",
                "Request",
                header.MessageName,
                "reply-error",
                "no-handler",
                channelName);
            ZLinkChannelReplyWriter.ReplyRequest(
                router,
                received,
                ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, error),
                null,
                null);
            return;
        }

        object? message;
        try
        {
            message = ZLinkEnvelopeCodec.DecodeBody(received.Parts, endpoint.MessageType);
        }
        catch (Exception ex)
        {
            var error = new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.PayloadDecodeFailed,
                $"PayloadDecodeFailed: failed to decode request payload for '{channelName}:{header.MessageName}'.",
                innerException: ex);
            ZLinkMessageFlowLogger.PayloadDecodeFailed(
                _logger,
                "Channel",
                "Request",
                header.MessageName,
                "reply-error",
                "payload-decode-failed",
                ex,
                channelName);
            ZLinkChannelReplyWriter.ReplyRequest(
                router,
                received,
                ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, error),
                null,
                null);
            return;
        }

        var context = new ZLinkRequestContext(
            channelName,
            header.MessageName,
            header.ContentType,
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

    private async Task DispatchDealerMeshRequestAsync(
        string channelName,
        IZLinkBackendDealerSocket dealer,
        Received received,
        CancellationToken cancellationToken)
    {
        ZLinkEnvelopeHeader header;
        try
        {
            header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
        }
        catch (Exception ex)
        {
            ZLinkMessageFlowLogger.PayloadDecodeFailed(
                _logger,
                "DealerMeshChannel",
                "Request",
                "unknown",
                "drop",
                "header-decode-failed",
                ex,
                channelName);
            return;
        }

        if (header.Kind != ZLinkMessageKind.Request)
        {
            ZLinkMessageFlowLogger.Dropped(
                _logger,
                LogLevel.Warning,
                "DealerMeshChannel",
                header.Kind.ToString(),
                header.MessageName,
                "invalid-request-kind",
                channelName);
            return;
        }

        if (!handlerRegistry.TryGetRequest(
                channelName,
                ResolveMappedGroups(channelName),
                header.MessageName,
                out var endpoint)
            || endpoint is null)
        {
            var error = new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.HandlerNotFound,
                $"No request handler is registered for '{channelName}:{header.MessageName}'.");
            ZLinkMessageFlowLogger.HandlerMissing(
                _logger,
                LogLevel.Warning,
                "DealerMeshChannel",
                "Request",
                header.MessageName,
                "reply-error",
                "no-handler",
                channelName);
            ZLinkChannelReplyWriter.ReplyDealerRequest(
                dealer,
                RequireRequestSeq(received),
                ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, error),
                null,
                null);
            return;
        }

        object? message;
        try
        {
            message = ZLinkEnvelopeCodec.DecodeBody(received.Parts, endpoint.MessageType);
        }
        catch (Exception ex)
        {
            var error = new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.PayloadDecodeFailed,
                $"PayloadDecodeFailed: failed to decode request payload for '{channelName}:{header.MessageName}'.",
                innerException: ex);
            ZLinkMessageFlowLogger.PayloadDecodeFailed(
                _logger,
                "DealerMeshChannel",
                "Request",
                header.MessageName,
                "reply-error",
                "payload-decode-failed",
                ex,
                channelName);
            ZLinkChannelReplyWriter.ReplyDealerRequest(
                dealer,
                RequireRequestSeq(received),
                ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, error),
                null,
                null);
            return;
        }

        var context = new ZLinkRequestContext(
            channelName,
            header.MessageName,
            header.ContentType,
            cancellationToken);

        try
        {
            var reply = await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                .ConfigureAwait(false);
            ZLinkChannelReplyWriter.ReplyDealerRequest(
                dealer,
                RequireRequestSeq(received),
                ZLinkChannelReplyWriter.CreateReplyHeader(ZLinkMessageKind.Response, channelName, header),
                reply,
                endpoint.ReplyType);
        }
        catch (Exception ex)
        {
            ZLinkChannelReplyWriter.ReplyDealerRequest(
                dealer,
                RequireRequestSeq(received),
                ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, ex),
                null,
                null);
        }
    }

    private async Task DispatchDealerMeshRawAsync(
        string channelName,
        Received received,
        CancellationToken cancellationToken)
    {
        ZLinkEnvelopeHeader header;
        try
        {
            header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
        }
        catch (Exception ex)
        {
            ZLinkMessageFlowLogger.PayloadDecodeFailed(
                _logger,
                "DealerMeshChannel",
                "Send",
                "unknown",
                "drop",
                "header-decode-failed",
                ex,
                channelName);
            return;
        }

        if (header.Kind == ZLinkMessageKind.Command)
        {
            await HandleCommandAsync(channelName, received.Parts, header, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        ZLinkMessageFlowLogger.Dropped(
            _logger,
            LogLevel.Warning,
            "DealerMeshChannel",
            header.Kind.ToString(),
            header.MessageName,
            "unsupported-dealer-raw-kind",
            channelName);
    }

    private async Task HandleRoutedSpotSendAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        CancellationToken cancellationToken)
    {
        var metadata = ZLinkRoutedSpotRelayPackets.DecodeMetadata(received);
        var targetNodeRid = runtime.ResolveAcceptedSpotRouteNodeRid(channelName);
        var targetSpotRid = RoutingId.From(metadata.TargetSpotRid);
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
            var targetSpotRid = RoutingId.From(metadata.TargetSpotRid);
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
        if (!handlerRegistry.TryGetCommand(
                channelName,
                ResolveMappedGroups(channelName),
                header.MessageName,
                out var endpoint)
            || endpoint is null)
        {
            ZLinkMessageFlowLogger.Dropped(
                _logger,
                registration.DispatchOptions.Unhandled.SendLogLevel,
                "Channel",
                "Send",
                header.MessageName,
                "no-handler",
                channelName);
            return;
        }

        var message = ZLinkEnvelopeCodec.DecodeBody(parts, endpoint.MessageType);
        var context = new ZLinkSendContext(
            channelName,
            header.MessageName,
            header.ContentType,
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

    private static ulong RequireRequestSeq(Received received)
    {
        return received.RequestSeq
            ?? throw new InvalidOperationException(
                "Dealer mesh request requires a request sequence.");
    }
}
