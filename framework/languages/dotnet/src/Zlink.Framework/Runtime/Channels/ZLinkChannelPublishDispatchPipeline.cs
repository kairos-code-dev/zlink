using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Handlers;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelPublishDispatchPipeline(
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
        TopicMessage topicMessage,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var endpoints = handlerRegistry.GetPublishes(
            channelName,
            resolveMappedGroups(channelName),
            header.MessageName);
        if (endpoints.Count == 0)
        {
            ZLinkMessageFlowLogger.Dropped(
                logger,
                unhandledLogLevel,
                "Channel",
                "Publish",
                header.MessageName,
                "no-handler",
                channelName);
            dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
                ZLinkDispatchErrorSurface.Channel,
                ZLinkDispatchMessageKind.Publish,
                ZLinkDispatchErrorReason.HandlerMissing,
                ZLinkDispatchErrorAction.Drop,
                header.MessageName,
                ChannelName: channelName,
                Topic: topicMessage.Topic,
                SourceRid: header.Source,
                CorrelationId: header.CorrelationId));
            return;
        }

        Dictionary<Type, object?>? decodedMessages = null;
        foreach (var endpoint in endpoints)
        {
            decodedMessages ??= new Dictionary<Type, object?>();
            if (!decodedMessages.TryGetValue(endpoint.MessageType, out var message))
            {
                try
                {
                    message = ZLinkEnvelopeCodec.DecodeBody(topicMessage.Parts, endpoint.MessageType, codecs);
                }
                catch (Exception ex)
                {
                    ZLinkMessageFlowLogger.PayloadDecodeFailed(
                        logger,
                        "Channel",
                        "Publish",
                        header.MessageName,
                        "drop",
                        "payload-decode-failed",
                        ex,
                        channelName);
                    dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
                        ZLinkDispatchErrorSurface.Channel,
                        ZLinkDispatchMessageKind.Publish,
                        ZLinkDispatchErrorReason.PayloadDecodeFailed,
                        ZLinkDispatchErrorAction.Drop,
                        header.MessageName,
                        ChannelName: channelName,
                        Topic: topicMessage.Topic,
                        SourceRid: header.Source,
                        CorrelationId: header.CorrelationId,
                        Exception: ex));
                    return;
                }

                decodedMessages.Add(endpoint.MessageType, message);
            }

            var context = new ZLinkPublishContext(
                channelName,
                header.MessageName,
                header.ContentType,
                topicMessage.Topic,
                header.Source,
                cancellationToken);
            try
            {
                await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                    .ConfigureAwait(false);

                if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowPhase.Dispatched))
                {
                    dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowPhase.Dispatched,
                        ZLinkDispatchErrorSurface.Channel,
                        ZLinkDispatchMessageKind.Publish,
                        PacketName: header.MessageName,
                        ChannelName: channelName,
                        Topic: topicMessage.Topic,
                        SourceRid: header.Source,
                        CorrelationId: header.CorrelationId));
                }
            }
            catch (Exception ex)
            {
                ZLinkMessageFlowLogger.Rejected(
                    logger,
                    LogLevel.Error,
                    "Channel",
                    "Publish",
                    header.MessageName,
                    "handler-exception",
                    ex,
                    channelName);
                dispatchErrors.Report(new ZLinkMessageDispatchErrorEvent(
                    ZLinkDispatchErrorSurface.Channel,
                    ZLinkDispatchMessageKind.Publish,
                    ZLinkDispatchErrorReason.HandlerException,
                    ZLinkDispatchErrorAction.Drop,
                    header.MessageName,
                    ChannelName: channelName,
                    Topic: topicMessage.Topic,
                    SourceRid: header.Source,
                    CorrelationId: header.CorrelationId,
                    Exception: ex));
            }
        }
    }
}
