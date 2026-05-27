using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Zlink.Framework.Runtime.Core;
using Zlink.Framework.Runtime.Diagnostics;
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
    private readonly ZLinkSpotRouteRelayIngressTransport _spotRouteRelay = new(runtime, registration);
    private readonly ILogger<ZLinkChannelPacketDispatcher> _logger =
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance;
    private readonly ZLinkChannelRequestDispatchPipeline _requestPipeline = new(
        handlerRegistry,
        dispatcher,
        channelName => ResolveMappedGroups(registration, channelName),
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance);
    private readonly ZLinkChannelCommandDispatchPipeline _commandPipeline = new(
        handlerRegistry,
        dispatcher,
        channelName => ResolveMappedGroups(registration, channelName),
        registration.DispatchOptions.Unhandled.SendLogLevel,
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance);
    private readonly ZLinkChannelPublishDispatchPipeline _publishPipeline = new(
        handlerRegistry,
        dispatcher,
        channelName => ResolveMappedGroups(registration, channelName),
        registration.DispatchOptions.Unhandled.PublishLogLevel,
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance);

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
                    await _spotRouteRelay.HandleRequestAsync(channelName, router, received, header, cancellationToken)
                        .ConfigureAwait(false);
                    return;
                }

                await HandleRequestAsync(channelName, router, received, header, cancellationToken)
                    .ConfigureAwait(false);
                break;
            case ZLinkMessageKind.Command:
                if (header.MessageName == ZLinkRoutedSpotRelayPackets.SendPacketName)
                {
                    await _spotRouteRelay.HandleSendAsync(channelName, router, received, cancellationToken)
                        .ConfigureAwait(false);
                    return;
                }

                await _commandPipeline.DispatchAsync(
                        channelName,
                        "Channel",
                        received.Parts,
                        header,
                        cancellationToken)
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
        await _publishPipeline.DispatchAsync(
                channelName,
                topicMessage,
                header,
                cancellationToken)
            .ConfigureAwait(false);
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
        await _requestPipeline.DispatchAsync(
                channelName,
                "Channel",
                received,
                header,
                (replyHeader, reply, replyType) => ZLinkChannelReplyWriter.ReplyRequest(
                    router,
                    received,
                    replyHeader,
                    reply,
                    replyType),
                errorHeader => ZLinkChannelReplyWriter.ReplyRequest(
                    router,
                    received,
                    errorHeader,
                    null,
                    null),
                cancellationToken)
            .ConfigureAwait(false);
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

        var requestSeq = RequireRequestSeq(received);
        await _requestPipeline.DispatchAsync(
                channelName,
                "DealerMeshChannel",
                received,
                header,
                (replyHeader, reply, replyType) => ZLinkChannelReplyWriter.ReplyDealerRequest(
                    dealer,
                    requestSeq,
                    replyHeader,
                    reply,
                    replyType),
                errorHeader => ZLinkChannelReplyWriter.ReplyDealerRequest(
                    dealer,
                    requestSeq,
                    errorHeader,
                    null,
                    null),
                cancellationToken)
            .ConfigureAwait(false);
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
            await _commandPipeline.DispatchAsync(
                    channelName,
                    "DealerMeshChannel",
                    received.Parts,
                    header,
                    cancellationToken)
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

    private static IReadOnlySet<string> ResolveMappedGroups(
        ZLinkFrameworkRegistration registration,
        string channelName)
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
