using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRoutePacketDispatcher(
    string routerChannelId,
    IZLinkBackendRouterSocket router,
    ZLinkRouteHandlerRegistry handlers,
    ZLinkRouteHandlerInvoker handlerInvoker,
    ZLinkCodecRegistryBuilder codecs,
    IZLinkRouteInternalPacketDispatcher internalPackets,
    ZLinkDispatchErrorReporter dispatchErrors,
    ZLinkFrameworkRuntime? runtime,
    ILogger<ZLinkRoutePacketDispatcher>? logger = null)
{
    private readonly ILogger<ZLinkRoutePacketDispatcher> _logger =
        logger ?? NullLogger<ZLinkRoutePacketDispatcher>.Instance;

    public async ValueTask DispatchAsync(
        Received received,
        CancellationToken cancellationToken)
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
            dispatchErrors.Flow.GenerationEnabled,
            ZLinkFlowOrigin.Inbound);

        CreateScope(
                header,
                header.Kind == ZLinkMessageKind.Request
                    ? ZLinkDispatchMessageKind.Request
                    : ZLinkDispatchMessageKind.Send,
                header.Kind == ZLinkMessageKind.Request ? "Request" : "Send")
            .Trace(dispatchErrors, ZLinkMessageFlowOutcome.Received);

        switch (header.Kind)
        {
            case ZLinkMessageKind.Command:
                await DispatchSendAsync(received, header, cancellationToken).ConfigureAwait(false);
                return;
            case ZLinkMessageKind.Request:
                await DispatchRequestAsync(received, header, cancellationToken).ConfigureAwait(false);
                break;
        }
    }

    private async ValueTask DispatchSendAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (internalPackets.CanHandleSend(header.MessageName))
        {
            await internalPackets.DispatchSendAsync(received, cancellationToken).ConfigureAwait(false);
            return;
        }

        ZLinkFrameworkRuntime.ZLinkRuntimeOperationLease operation;
        if (runtime is null)
            operation = new ZLinkFrameworkRuntime.ZLinkRuntimeOperationLease();
        else if (!runtime.TryEnterInboundOperation(countAsRequest: false, out operation))
        {
            CreateScope(header, ZLinkDispatchMessageKind.Send, "Send")
                .Dropped(_logger, dispatchErrors, LogLevel.Information);
            return;
        }
        using (operation)
        {

        if (!handlers.TryGet(
                routerChannelId,
                ZLinkMessageKind.Command,
                header.MessageName,
                out var descriptor)
            || descriptor is null)
        {
            CreateScope(header, ZLinkDispatchMessageKind.Send, "Send")
                .Dropped(_logger, dispatchErrors, LogLevel.Warning);
            return;
        }

        var sourceRid = RequireSourceRoutingId(received, "Route send");
        var source = sourceRid.ToString();
        var scope = CreateScope(
            header,
            ZLinkDispatchMessageKind.Send,
            "Send",
            sourceRid: source,
            logSpotRid: source);

        try
        {
            await handlerInvoker.InvokeSendAsync(
                    descriptor,
                    routerChannelId,
                    sourceRid,
                    header,
                    received.Parts,
                    cancellationToken)
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
        }
    }

    private async ValueTask DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (internalPackets.CanHandleRequest(header.MessageName))
        {
            await DispatchInternalRequestAsync(
                    received,
                    header,
                    cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var sourceRid = RequireSourceRoutingId(received, "Route request");
        var source = sourceRid.ToString();
        var scope = CreateScope(
            header,
            ZLinkDispatchMessageKind.Request,
            "Request",
            sourceRid: source);

        ZLinkFrameworkRuntime.ZLinkRuntimeOperationLease operation;
        if (runtime is null)
            operation = new ZLinkFrameworkRuntime.ZLinkRuntimeOperationLease();
        else if (!runtime.TryEnterInboundOperation(countAsRequest: true, out operation))
        {
            var error = new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                "The framework is draining and no longer accepts new requests.");
            ReplyError(sourceRid, received.RequestSeq, header, error);
            scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Error);
            return;
        }
        using (operation)
        {

        if (!handlers.TryGet(
                routerChannelId,
                ZLinkMessageKind.Request,
                header.MessageName,
                out var descriptor)
            || descriptor is null)
        {
            var error = new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteHandlerNotFound,
                $"No routed request handler is registered for '{routerChannelId}:{header.MessageName}'.");
            scope.HandlerMissing(
                _logger,
                dispatchErrors,
                LogLevel.Error,
                ZLinkDispatchErrorAction.ReplyError,
                error);
            ReplyError(sourceRid, received.RequestSeq, header, error);
            return;
        }

        try
        {
            var reply = await handlerInvoker.InvokeRequestAsync(
                    descriptor,
                    routerChannelId,
                    sourceRid,
                    header,
                    received.Parts,
                    cancellationToken)
                .ConfigureAwait(false);
            Reply(sourceRid, received.RequestSeq, header, reply.Message, reply.MessageType);

            scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Replied);
        }
        catch (Exception ex)
        {
            ReplyError(sourceRid, received.RequestSeq, header, ex);
            scope.HandlerException(
                _logger,
                dispatchErrors,
                null,
                ZLinkDispatchErrorAction.ReplyError,
                ex);
        }
        }
    }

    private async ValueTask DispatchInternalRequestAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var sourceRid = RequireSourceRoutingId(received, "Internal routed request");
        var scope = CreateScope(
            header,
            ZLinkDispatchMessageKind.Request,
            "Request",
            sourceRid: sourceRid.ToString());

        try
        {
            using var reply = await internalPackets.DispatchRequestAsync(received, header, cancellationToken)
                .ConfigureAwait(false);
            ReplyRaw(sourceRid, received.RequestSeq, header, reply);

            scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Replied);
        }
        catch (Exception ex)
        {
            ReplyError(sourceRid, received.RequestSeq, header, ex);
            scope.HandlerException(
                _logger,
                dispatchErrors,
                null,
                ZLinkDispatchErrorAction.ReplyError,
                ex);
        }
    }

    private void Reply(
        RoutingId sourceRid,
        ulong? requestSeq,
        ZLinkEnvelopeHeader requestHeader,
        object? reply,
        Type? replyType)
    {
        ZLinkChannelReplyWriter.ReplyEnvelope(
            router,
            sourceRid,
            requestSeq,
            ZLinkChannelReplyWriter.CreateReplyHeader(
                ZLinkMessageKind.Response,
                routerChannelId,
                requestHeader),
            reply,
            replyType,
            codecs);
    }

    private ZLinkDispatchFlowScope CreateScope(
        ZLinkEnvelopeHeader header,
        ZLinkDispatchMessageKind messageKind,
        string kindName,
        string? sourceRid = null,
        string? spotRid = null,
        string? logSpotRid = null)
    {
        return new ZLinkDispatchFlowScope(
            ZLinkDispatchErrorSurface.RouteMeshChannel,
            "RouteMeshChannel",
            messageKind,
            kindName,
            header.MessageName,
            routerChannelId,
            header.ContentType,
            header.CorrelationId,
            sourceRid: sourceRid,
            spotRid: spotRid,
            logSpotRid: logSpotRid);
    }

    private static RoutingId RequireSourceRoutingId(
        Received received,
        string operationName)
    {
        return received.RoutingId
               ?? throw new InvalidOperationException($"{operationName} requires a source routing id.");
    }

    private void HandleProtocolError(
        Received received,
        ZLinkEnvelopeProtocolException protocolError)
    {
        var header = protocolError.Header;
        var isRequest = received.RequestSeq.HasValue;
        var validFlow = ZLinkEnvelopeCodec.ValidFlow(header);
        using var flow = ZLinkFlowContext.Enter(
            validFlow.FlowId,
            validFlow.FlowOrigin,
            dispatchErrors.Flow.GenerationEnabled,
            ZLinkFlowOrigin.Inbound);
        dispatchErrors.Report(new ZLinkDispatchFailure(
            ZLinkDispatchErrorSurface.RouteMeshChannel,
            isRequest
                ? ZLinkDispatchMessageKind.Request
                : ZLinkDispatchMessageKind.Send,
            ZLinkDispatchErrorReason.InvalidFrame,
            isRequest
                ? ZLinkDispatchErrorAction.ReplyError
                : ZLinkDispatchErrorAction.Drop,
            header.MessageName,
            routerChannelId,
            SourceRid: received.RoutingId?.ToString(),
            CorrelationId: header.CorrelationId,
            Exception: protocolError));
        if (!isRequest || received.RoutingId is not { } sourceRid) return;

        ZLinkChannelReplyWriter.ReplyEnvelope(
            router,
            sourceRid,
            received.RequestSeq,
            ZLinkChannelReplyWriter.CreateProtocolErrorHeader(
                routerChannelId,
                header,
                protocolError.Message),
            null,
            null);
    }

    private void ReplyRaw(
        RoutingId sourceRid,
        ulong? requestSeq,
        ZLinkEnvelopeHeader requestHeader,
        Message reply)
    {
        ZLinkChannelReplyWriter.ReplyRawEnvelope(
            router,
            sourceRid,
            requestSeq,
            ZLinkChannelReplyWriter.CreateReplyHeader(
                ZLinkMessageKind.Response,
                routerChannelId,
                requestHeader),
            reply);
    }

    private void ReplyError(
        RoutingId sourceRid,
        ulong? requestSeq,
        ZLinkEnvelopeHeader requestHeader,
        Exception exception)
    {
        ZLinkChannelReplyWriter.ReplyEnvelope(
            router,
            sourceRid,
            requestSeq,
            ZLinkChannelReplyWriter.CreateErrorHeader(routerChannelId, requestHeader, exception),
            null,
            null);
    }
}
