using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRequestDispatchPipeline(
    string meshName,
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    Func<string, IReadOnlySet<string>> resolveMappedGroups,
    ZLinkCodecRegistryBuilder codecs,
    ZLinkDispatchErrorReporter dispatchErrors,
    ILogger logger)
{
    public async Task DispatchAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        ZLinkEnvelopeHeader header,
        Action<ZLinkEnvelopeHeader, object?, Type?> reply,
        Action<ZLinkEnvelopeHeader> replyError,
        CancellationToken cancellationToken,
        ZLinkMessageMetadata? metadata = null)
    {
        var scope = new ZLinkDispatchFlowScope(
            ZLinkDispatchErrorSurface.Channel,
            "Channel",
            ZLinkDispatchMessageKind.Request,
            "Request",
            header.MessageName,
            channelName,
            header.ContentType,
            header.CorrelationId);
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
            scope.HandlerMissing(
                logger,
                dispatchErrors,
                LogLevel.Error,
                ZLinkDispatchErrorAction.ReplyError,
                error);
            replyError(ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, error));
            return;
        }

        ZLinkFrameworkException? decodeError = null;
        if (!scope.TryDecode(
                parts,
                endpoint.MessageType,
                scope.ContentType!,
                codecs,
                logger,
                dispatchErrors,
                ZLinkDispatchErrorAction.ReplyError,
                out var message,
                ex => decodeError = new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.PayloadDecodeFailed,
                    $"PayloadDecodeFailed: failed to decode request payload for '{channelName}:{header.MessageName}'.",
                    innerException: ex)))
        {
            replyError(ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, decodeError!));
            return;
        }

        var context = new ZLinkRequestContext(
            meshName,
            scope.ChannelName,
            scope.PacketName!,
            scope.ContentType,
            cancellationToken,
            metadata);

        try
        {
            var response = await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                .ConfigureAwait(false);
            reply(
                ZLinkChannelReplyWriter.CreateReplyHeader(ZLinkMessageKind.Response, channelName, header),
                response,
                endpoint.ReplyType);

            scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Replied);
        }
        catch (Exception ex)
        {
            replyError(ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, ex));
            scope.HandlerException(
                logger,
                dispatchErrors,
                null,
                ZLinkDispatchErrorAction.ReplyError,
                ex);
        }
    }
}
