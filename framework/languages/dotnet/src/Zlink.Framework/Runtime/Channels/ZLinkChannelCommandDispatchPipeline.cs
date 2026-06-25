using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Handlers;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelCommandDispatchPipeline(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    Func<string, IReadOnlySet<string>> resolveMappedGroups,
    LogLevel unhandledLogLevel,
    ZLinkDispatchErrorReporter dispatchErrors,
    ZLinkCodecRegistryBuilder codecs,
    ILogger logger)
{
    public async Task DispatchAsync(
        string channelName,
        string transportName,
        IReadOnlyList<Message> parts,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (!handlerRegistry.TryGetCommand(
                channelName,
                resolveMappedGroups(channelName),
                header.MessageName,
                out var endpoint)
            || endpoint is null)
        {
            ZLinkMessageFlowLogger.Dropped(
                logger,
                unhandledLogLevel,
                transportName,
                "Send",
                header.MessageName,
                "no-handler",
                channelName);
            dispatchErrors.Report(new ZLinkDispatchFailure(
                ResolveSurface(transportName),
                ZLinkDispatchMessageKind.Send,
                ZLinkDispatchErrorReason.HandlerMissing,
                ZLinkDispatchErrorAction.Drop,
                header.MessageName,
                ChannelName: channelName,
                CorrelationId: header.CorrelationId));
            return;
        }

        object? message;
        try
        {
            message = ZLinkEnvelopeCodec.DecodeBody(parts, endpoint.MessageType, codecs);
        }
        catch (Exception ex)
        {
            ZLinkMessageFlowLogger.PayloadDecodeFailed(
                logger,
                transportName,
                "Send",
                header.MessageName,
                "drop",
                "payload-decode-failed",
                ex,
                channelName);
            dispatchErrors.Report(new ZLinkDispatchFailure(
                ResolveSurface(transportName),
                ZLinkDispatchMessageKind.Send,
                ZLinkDispatchErrorReason.PayloadDecodeFailed,
                ZLinkDispatchErrorAction.Drop,
                header.MessageName,
                ChannelName: channelName,
                CorrelationId: header.CorrelationId,
                Exception: ex));
            return;
        }

        var context = new ZLinkSendContext(
            channelName,
            header.MessageName,
            header.ContentType,
            cancellationToken);
        try
        {
            await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                .ConfigureAwait(false);

            if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Dispatched))
            {
                dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.Dispatched,
                    ResolveSurface(transportName),
                    ZLinkDispatchMessageKind.Send,
                    PacketName: header.MessageName,
                    ChannelName: channelName,
                    CorrelationId: header.CorrelationId));
            }
        }
        catch (Exception ex)
        {
            ZLinkMessageFlowLogger.Rejected(
                logger,
                LogLevel.Error,
                transportName,
                "Send",
                header.MessageName,
                "handler-exception",
                ex,
                channelName);
            dispatchErrors.Report(new ZLinkDispatchFailure(
                ResolveSurface(transportName),
                ZLinkDispatchMessageKind.Send,
                ZLinkDispatchErrorReason.HandlerException,
                ZLinkDispatchErrorAction.Drop,
                header.MessageName,
                ChannelName: channelName,
                CorrelationId: header.CorrelationId,
                Exception: ex));
        }
    }

    private static ZLinkDispatchErrorSurface ResolveSurface(string transportName)
    {
        return ZLinkDispatchErrorSurface.Channel;
    }
}
