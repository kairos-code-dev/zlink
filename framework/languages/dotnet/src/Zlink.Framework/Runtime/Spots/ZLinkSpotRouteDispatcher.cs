using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Zlink.Framework.Runtime.Diagnostics;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotRouteDispatcher(
    string channelName,
    string spotRid,
    ZLinkSpotPacketRegistry packets,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker,
    ZLinkCodecRegistryBuilder codecs,
    ZLinkDispatchErrorReporter dispatchErrors,
    Func<Received, ZLinkEnvelopeHeader, CancellationToken, ValueTask<bool>>? internalPackets = null,
    ILogger<ZLinkSpotRouteDispatcher>? logger = null)
{
    private readonly ILogger<ZLinkSpotRouteDispatcher> _logger =
        logger ?? NullLogger<ZLinkSpotRouteDispatcher>.Instance;

    public async ValueTask DispatchAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        using (received)
        {
            if (received.Parts.Count == 0)
            {
                return;
            }

            var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);

            if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Received))
            {
                dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.Received,
                    ZLinkDispatchErrorSurface.SpotRoute,
                    header.Kind == ZLinkMessageKind.Request
                        ? ZLinkDispatchMessageKind.Request
                        : ZLinkDispatchMessageKind.Send,
                    PacketName: header.MessageName,
                    ChannelName: channelName,
                    SpotRid: received.SpotRid?.ToString() ?? spotRid,
                    CorrelationId: header.CorrelationId));
            }

            if (internalPackets is not null
                && await internalPackets(received, header, cancellationToken).ConfigureAwait(false))
            {
                return;
            }

            if (!packets.TryResolve(header, out var descriptor) || descriptor is null)
            {
                var dispatchSpotRid = received.SpotRid?.ToString() ?? spotRid;
                if (header.Kind == ZLinkMessageKind.Request)
                {
                    ZLinkMessageFlowLogger.HandlerMissing(
                        _logger,
                        LogLevel.Error,
                        "Spot",
                        "Request",
                        header.MessageName,
                        "reply-error",
                        "no-handler",
                        channelName,
                        spotRid: dispatchSpotRid);
                    ReplyError(
                        received,
                        header,
                        new ZLinkFrameworkException(
	                            ZLinkFrameworkErrorKind.HandlerNotFound,
	                            $"No SPOT route request handler is registered for '{channelName}:{header.MessageName}'."));
                    dispatchErrors.Report(new ZLinkDispatchFailure(
                        ZLinkDispatchErrorSurface.SpotRoute,
                        ZLinkDispatchMessageKind.Request,
                        ZLinkDispatchErrorReason.HandlerMissing,
                        ZLinkDispatchErrorAction.ReplyError,
                        header.MessageName,
                        ChannelName: channelName,
                        SpotRid: dispatchSpotRid,
                        CorrelationId: header.CorrelationId,
                        Exception: new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.HandlerNotFound,
                            $"No SPOT route request handler is registered for '{channelName}:{header.MessageName}'.")));
                }
                else
                {
                    ZLinkMessageFlowLogger.Dropped(
                        _logger,
                        LogLevel.Warning,
                        "Spot",
                        "Send",
                        header.MessageName,
                        "no-handler",
                        channelName,
                        spotRid: dispatchSpotRid);
                    dispatchErrors.Report(new ZLinkDispatchFailure(
                        ZLinkDispatchErrorSurface.SpotRoute,
                        ZLinkDispatchMessageKind.Send,
                        ZLinkDispatchErrorReason.HandlerMissing,
                        ZLinkDispatchErrorAction.Drop,
                        header.MessageName,
                        ChannelName: channelName,
                        SpotRid: dispatchSpotRid,
                        CorrelationId: header.CorrelationId));
                }

                return;
            }

            object? message;
            try
            {
                message = ZLinkEnvelopeCodec.DecodeBody(received.Parts, descriptor.MessageType, codecs);
            }
            catch (Exception ex) when (descriptor.IsRequest)
            {
                ZLinkMessageFlowLogger.PayloadDecodeFailed(
                    _logger,
                    "Spot",
                    "Request",
                    header.MessageName,
                    "reply-error",
                    "payload-decode-failed",
                    ex,
                    channelName);
                ReplyError(
                    received,
                    header,
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.PayloadDecodeFailed,
	                        $"PayloadDecodeFailed: failed to decode SPOT route request payload for '{channelName}:{header.MessageName}'.",
	                        innerException: ex));
                dispatchErrors.Report(new ZLinkDispatchFailure(
                    ZLinkDispatchErrorSurface.SpotRoute,
                    ZLinkDispatchMessageKind.Request,
                    ZLinkDispatchErrorReason.PayloadDecodeFailed,
                    ZLinkDispatchErrorAction.ReplyError,
                    header.MessageName,
                    ChannelName: channelName,
                    CorrelationId: header.CorrelationId,
                    Exception: ex));
                return;
            }

            if (!descriptor.IsRequest)
            {
                try
                {
                    await handlerInvoker()
                        .InvokePacketAsync(descriptor, message, cancellationToken)
                        .ConfigureAwait(false);

                    if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Dispatched))
                    {
                        dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                            ZLinkMessageFlowOutcome.Dispatched,
                            ZLinkDispatchErrorSurface.SpotRoute,
                            ZLinkDispatchMessageKind.Send,
                            PacketName: header.MessageName,
                            ChannelName: channelName,
                            SpotRid: spotRid,
                            CorrelationId: header.CorrelationId));
                    }
                }
                catch (Exception ex)
                {
                    ZLinkMessageFlowLogger.Rejected(
                        _logger,
                        LogLevel.Error,
                        "Spot",
                        "Send",
                        header.MessageName,
                        "handler-exception",
                        ex,
                        channelName);
                    dispatchErrors.Report(new ZLinkDispatchFailure(
                        ZLinkDispatchErrorSurface.SpotRoute,
                        ZLinkDispatchMessageKind.Send,
                        ZLinkDispatchErrorReason.HandlerException,
                        ZLinkDispatchErrorAction.Drop,
                        header.MessageName,
                        ChannelName: channelName,
                        CorrelationId: header.CorrelationId,
                        Exception: ex));
                }
                return;
            }

            IReadOnlyList<Message> replyParts;
            try
            {
                var reply = await handlerInvoker()
                    .InvokeRequestAsync(descriptor, message, cancellationToken)
                    .ConfigureAwait(false);
                replyParts = ZLinkSpotReplyEnvelope.EncodeResponseParts(
                    channelName,
                    descriptor.MessageName,
                    header.CorrelationId,
                    reply,
                    descriptor.ReplyType,
                    codecs);

                if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Replied))
                {
                    dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowOutcome.Replied,
                        ZLinkDispatchErrorSurface.SpotRoute,
                        ZLinkDispatchMessageKind.Request,
                        PacketName: header.MessageName,
                        ChannelName: channelName,
                        SpotRid: spotRid,
                        CorrelationId: header.CorrelationId));
                }
            }
            catch (Exception ex)
            {
                replyParts = ZLinkSpotReplyEnvelope.EncodeErrorParts(
                    channelName,
                    descriptor.MessageName,
                    header.CorrelationId,
                    ex);
                dispatchErrors.Report(new ZLinkDispatchFailure(
                    ZLinkDispatchErrorSurface.SpotRoute,
                    ZLinkDispatchMessageKind.Request,
                    ZLinkDispatchErrorReason.HandlerException,
                    ZLinkDispatchErrorAction.ReplyError,
                    header.MessageName,
                    ChannelName: channelName,
                    CorrelationId: header.CorrelationId,
                    Exception: ex));
            }

            try
            {
                received.Reply()
                    .Message(replyParts[0])
                    .Message(replyParts[1])
                    .Submit();
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(replyParts);
            }
        }
    }

    private void ReplyError(
        Received received,
        ZLinkEnvelopeHeader header,
        Exception exception)
    {
        var replyParts = ZLinkSpotReplyEnvelope.EncodeErrorParts(
            channelName,
            header.MessageName,
            header.CorrelationId,
            exception);
        try
        {
            received.Reply()
                .Message(replyParts[0])
                .Message(replyParts[1])
                .Submit();
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}
