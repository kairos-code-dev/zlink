using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Diagnostics;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRoutePacketDispatcher(
    string routerChannelId,
    IZLinkBackendRouterSocket router,
    ZLinkRouteHandlerRegistry handlers,
    ZLinkRouteHandlerInvoker handlerInvoker,
    IZLinkRouteInternalPacketDispatcher internalPackets,
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
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
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

        if (!handlers.TryGet(
                routerChannelId,
                ZLinkMessageKind.Command,
                header.MessageName,
                out var descriptor)
            || descriptor is null)
        {
            ZLinkMessageFlowLogger.Dropped(
                _logger,
                LogLevel.Warning,
                "RouteMeshChannel",
                "Send",
                header.MessageName,
                "no-handler",
                routerChannelId);
            return;
        }

        var sourceRid = RequireSourceRoutingId(received, "Route send");

        await handlerInvoker.InvokeSendAsync(
                descriptor,
                routerChannelId,
                sourceRid,
                header,
                received.Parts,
                cancellationToken)
            .ConfigureAwait(false);
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
                cancellationToken,
                (_, requestHeader, token) => internalPackets.DispatchRequestAsync(received, requestHeader, token))
                .ConfigureAwait(false);
            return;
        }

        var sourceRid = RequireSourceRoutingId(received, "Route request");

        if (!handlers.TryGet(
                routerChannelId,
                ZLinkMessageKind.Request,
                header.MessageName,
                out var descriptor)
            || descriptor is null)
        {
            ZLinkMessageFlowLogger.HandlerMissing(
                _logger,
                LogLevel.Warning,
                "RouteMeshChannel",
                "Request",
                header.MessageName,
                "reply-error",
                "no-handler",
                routerChannelId);
            ReplyError(
                sourceRid,
                received.RequestSeq,
                header,
                new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteHandlerNotFound,
                    $"No routed request handler is registered for '{routerChannelId}:{header.MessageName}'."));
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
        }
        catch (Exception ex)
        {
            ReplyError(sourceRid, received.RequestSeq, header, ex);
        }
    }

    private async ValueTask DispatchInternalRequestAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken,
        Func<RoutingId, ZLinkEnvelopeHeader, CancellationToken, ValueTask<Message>> dispatch)
    {
        var sourceRid = RequireSourceRoutingId(received, "Internal routed request");

        try
        {
            using var reply = await dispatch(sourceRid, header, cancellationToken).ConfigureAwait(false);
            ReplyRaw(sourceRid, received.RequestSeq, header, reply);
        }
        catch (Exception ex)
        {
            ReplyError(sourceRid, received.RequestSeq, header, ex);
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
            replyType);
    }

    private static RoutingId RequireSourceRoutingId(
        Received received,
        string operationName)
    {
        return received.RoutingId
            ?? throw new InvalidOperationException($"{operationName} requires a source routing id.");
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
