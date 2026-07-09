using Microsoft.Extensions.Logging;

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
        var scope = new ZLinkDispatchFlowScope(
            ZLinkDispatchErrorSurface.Channel,
            transportName,
            ZLinkDispatchMessageKind.Send,
            "Send",
            header.MessageName,
            channelName,
            header.ContentType,
            header.CorrelationId);
        if (!handlerRegistry.TryGetCommand(
                channelName,
                resolveMappedGroups(channelName),
                header.MessageName,
                out var endpoint)
            || endpoint is null)
        {
            scope.Dropped(logger, dispatchErrors, unhandledLogLevel);
            return;
        }

        if (!scope.TryDecode(
                parts,
                endpoint.MessageType,
                scope.ContentType!,
                codecs,
                logger,
                dispatchErrors,
                ZLinkDispatchErrorAction.Drop,
                out var message))
            return;

        var context = new ZLinkSendContext(
            scope.ChannelName,
            scope.PacketName,
            scope.ContentType,
            cancellationToken);
        try
        {
            await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                .ConfigureAwait(false);

            scope.Trace(dispatchErrors, ZLinkMessageFlowOutcome.Dispatched);
        }
        catch (Exception ex)
        {
            scope.HandlerException(
                logger,
                dispatchErrors,
                LogLevel.Error,
                ZLinkDispatchErrorAction.Drop,
                ex);
        }
    }
}
