using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotRouteDispatcher(
    string channelName,
    string spotRid,
    ZLinkSpotPacketRegistry packets,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker,
    ZLinkCodecRegistryBuilder codecs,
    ZLinkDispatchErrorReporter dispatchErrors,
    Func<ZLinkBackendRouteReceived, ZLinkEnvelopeHeader, CancellationToken, ValueTask<bool>>? internalPackets = null,
    ILogger<ZLinkSpotRouteDispatcher>? logger = null)
{
    private readonly ILogger<ZLinkSpotRouteDispatcher> _logger =
        logger ?? NullLogger<ZLinkSpotRouteDispatcher>.Instance;

    public async ValueTask DispatchAsync(
        ZLinkBackendRouteReceived received,
        CancellationToken cancellationToken)
    {
        using (received)
        {
        if (received.Parts.Count == 0)
        {
            HandleProtocolError(received, ZLinkEnvelopeCodec.MissingHeader());
            return;
        }

            ZLinkEnvelopeHeader header;
            try
            {
            header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
            ZLinkEnvelopeCodec.ValidateDispatchHeader(header);
            }
            catch (ZLinkEnvelopeProtocolException protocolError)
            {
                HandleProtocolError(received, protocolError);
                return;
            }
            using var currentFlow = ZLinkFlowContext.Enter(
                header.FlowId,
                header.FlowOrigin,
                dispatchErrors.Flow.CaptureEnabled,
                ZLinkFlowOrigin.Inbound);
            var dispatchSpotRid = received.SpotRid?.ToString() ?? spotRid;
            var kind = header.Kind == ZLinkMessageKind.Request
                ? ZLinkDispatchMessageKind.Request
                : ZLinkDispatchMessageKind.Send;
            var kindName = header.Kind == ZLinkMessageKind.Request ? "Request" : "Send";
            var scope = CreateScope(header, kind, kindName, dispatchSpotRid);

            scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Received);

            if (internalPackets is not null
                && await internalPackets(received, header, cancellationToken).ConfigureAwait(false))
                return;

            if (!packets.TryResolve(header, out var descriptor) || descriptor is null)
            {
                if (header.Kind == ZLinkMessageKind.Request)
                {
                    var error = new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.HandlerNotFound,
                        $"No SPOT route request handler is registered for '{channelName}:{header.MessageName}'.");
                    scope.HandlerMissing(
                        _logger,
                        dispatchErrors,
                        LogLevel.Error,
                        ZLinkDispatchErrorAction.ReplyError,
                        error);
                    ReplyError(received, header, error);
                }
                else
                {
                    scope.Dropped(_logger, dispatchErrors, LogLevel.Warning);
                }

                return;
            }

            object? message;
            if (descriptor.IsRequest)
            {
                ZLinkFrameworkException? decodeError = null;
                if (!scope.TryDecode(
                        received.Parts,
                        descriptor.MessageType,
                        header.ContentType,
                        codecs,
                        _logger,
                        dispatchErrors,
                        ZLinkDispatchErrorAction.ReplyError,
                        out message,
                        ex => decodeError = new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.PayloadDecodeFailed,
                        $"PayloadDecodeFailed: failed to decode SPOT route request payload for '{channelName}:{header.MessageName}'.",
                        innerException: ex)))
                {
                    ReplyError(received, header, decodeError!);
                    return;
                }
            }
            else
            {
                if (!scope.TryDecode(
                        received.Parts,
                        descriptor.MessageType,
                        header.ContentType,
                        codecs,
                        _logger,
                        dispatchErrors,
                        ZLinkDispatchErrorAction.Drop,
                        out message))
                    return;
            }

            if (!descriptor.IsRequest)
            {
                try
                {
                    await handlerInvoker()
                        .InvokePacketAsync(descriptor, message, cancellationToken)
                        .ConfigureAwait(false);

                    scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Dispatched);
                }
                catch (Exception ex)
                {
                    scope.HandlerException(
                        _logger,
                        dispatchErrors,
                        LogLevel.Error,
                        ZLinkDispatchErrorAction.Drop,
                        ex);
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

                scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Replied);
            }
            catch (Exception ex)
            {
                replyParts = ZLinkSpotReplyEnvelope.EncodeErrorParts(
                    channelName,
                    descriptor.MessageName,
                    header.CorrelationId,
                    ex);
                scope.HandlerException(
                    _logger,
                    dispatchErrors,
                    null,
                    ZLinkDispatchErrorAction.ReplyError,
                    ex);
            }

            ZLinkSpotReplySubmitter.SubmitAndDispose(received, replyParts);
        }
    }

    private ZLinkDispatchFlowScope CreateScope(
        ZLinkEnvelopeHeader header,
        ZLinkDispatchMessageKind kind,
        string kindName,
        string dispatchSpotRid)
    {
        return new ZLinkDispatchFlowScope(
            ZLinkDispatchErrorSurface.SpotRoute,
            "Spot",
            kind,
            kindName,
            header.MessageName,
            channelName,
            header.ContentType,
            header.CorrelationId,
            spotRid: dispatchSpotRid);
    }

    private void ReplyError(
        ZLinkBackendRouteReceived received,
        ZLinkEnvelopeHeader header,
        Exception exception)
    {
        var replyParts = ZLinkSpotReplyEnvelope.EncodeErrorParts(
            channelName,
            header.MessageName,
            header.CorrelationId,
            exception);
        ZLinkSpotReplySubmitter.SubmitAndDispose(received, replyParts);
    }

    private void HandleProtocolError(
        ZLinkBackendRouteReceived received,
        ZLinkEnvelopeProtocolException protocolError)
    {
        var header = protocolError.Header;
        var dispatchSpotRid = received.SpotRid?.ToString() ?? spotRid;
        var isRequest = received.RequestSeq.HasValue;
        var canReply = isRequest && ZLinkEnvelopeCodec.CanCorrelateReply(header);
        var validFlow = ZLinkEnvelopeCodec.ValidFlow(header);
        using var flow = ZLinkFlowContext.Enter(
            validFlow.FlowId,
            validFlow.FlowOrigin,
            dispatchErrors.Flow.CaptureEnabled,
            ZLinkFlowOrigin.Inbound);
        dispatchErrors.Report(new ZLinkDispatchFailure(
            ZLinkDispatchErrorSurface.SpotRoute,
            isRequest
                ? ZLinkDispatchMessageKind.Request
                : ZLinkDispatchMessageKind.Send,
            ZLinkDispatchErrorReason.InvalidFrame,
            canReply
                ? ZLinkDispatchErrorAction.ReplyError
                : ZLinkDispatchErrorAction.Drop,
            header.MessageName,
            channelName,
            SpotRid: dispatchSpotRid,
            CorrelationId: header.CorrelationId,
            Exception: protocolError));
        if (!canReply) return;

        var replyParts = ZLinkSpotReplyEnvelope.EncodeProtocolErrorParts(
            channelName,
            header,
            protocolError.Message);
        ZLinkSpotReplySubmitter.SubmitAndDispose(received, replyParts);
    }
}
