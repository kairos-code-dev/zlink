using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Handlers;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRequestDispatchPipeline(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    Func<string, IReadOnlySet<string>> resolveMappedGroups,
    ZLinkCodecRegistryBuilder codecs,
    ZLinkDispatchErrorReporter dispatchErrors,
    ILogger logger)
{
    public async Task DispatchAsync(
        string channelName,
        string transportName,
        Received received,
        ZLinkEnvelopeHeader header,
        Action<ZLinkEnvelopeHeader, object?, Type?> reply,
        Action<ZLinkEnvelopeHeader> replyError,
        CancellationToken cancellationToken)
    {
        if (!handlerRegistry.TryGetRequest(
                channelName,
                resolveMappedGroups(channelName),
                header.MessageName,
                out var endpoint)
            || endpoint is null)
        {
            var error = new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.HandlerNotFound,
                $"No request handler is registered for '{channelName}:{header.MessageName}'.");
            ZLinkMessageFlowLogger.HandlerMissing(
                logger,
                LogLevel.Error,
                transportName,
                "Request",
                header.MessageName,
                "reply-error",
                "no-handler",
                channelName);
            replyError(ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, error));
            dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
                ResolveSurface(transportName),
                ZLinkDispatchMessageKind.Request,
                ZLinkDispatchErrorReason.HandlerMissing,
                ZLinkDispatchErrorAction.ReplyError,
                header.MessageName,
                ChannelName: channelName,
                CorrelationId: header.CorrelationId,
                Exception: error));
            return;
        }

        object? message;
        try
        {
            message = ZLinkEnvelopeCodec.DecodeBody(received.Parts, endpoint.MessageType, codecs);
        }
        catch (Exception ex)
        {
            var error = new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.PayloadDecodeFailed,
                $"PayloadDecodeFailed: failed to decode request payload for '{channelName}:{header.MessageName}'.",
                innerException: ex);
            ZLinkMessageFlowLogger.PayloadDecodeFailed(
                logger,
                transportName,
                "Request",
                header.MessageName,
                "reply-error",
                "payload-decode-failed",
                ex,
                channelName);
            replyError(ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, error));
            dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
                ResolveSurface(transportName),
                ZLinkDispatchMessageKind.Request,
                ZLinkDispatchErrorReason.PayloadDecodeFailed,
                ZLinkDispatchErrorAction.ReplyError,
                header.MessageName,
                ChannelName: channelName,
                CorrelationId: header.CorrelationId,
                Exception: error));
            return;
        }

        var context = new ZLinkRequestContext(
            channelName,
            header.MessageName,
            header.ContentType,
            cancellationToken);

        try
        {
            var response = await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                .ConfigureAwait(false);
            reply(
                ZLinkChannelReplyWriter.CreateReplyHeader(ZLinkMessageKind.Response, channelName, header),
                response,
                endpoint.ReplyType);
        }
        catch (Exception ex)
        {
            replyError(ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, ex));
            dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
                ResolveSurface(transportName),
                ZLinkDispatchMessageKind.Request,
                ZLinkDispatchErrorReason.HandlerException,
                ZLinkDispatchErrorAction.ReplyError,
                header.MessageName,
                ChannelName: channelName,
                CorrelationId: header.CorrelationId,
                Exception: ex));
        }
    }

    private static ZLinkDispatchErrorSurface ResolveSurface(string transportName)
    {
        return string.Equals(transportName, "DealerMeshChannel", StringComparison.Ordinal)
            ? ZLinkDispatchErrorSurface.DealerMeshChannel
            : ZLinkDispatchErrorSurface.Channel;
    }
}
