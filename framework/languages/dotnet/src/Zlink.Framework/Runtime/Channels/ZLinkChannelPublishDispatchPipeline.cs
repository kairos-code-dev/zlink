using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Handlers;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelPublishDispatchPipeline(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    Func<string, IReadOnlySet<string>> resolveMappedGroups,
    LogLevel unhandledLogLevel,
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
            return;
        }

        Dictionary<Type, object?>? decodedMessages = null;
        foreach (var endpoint in endpoints)
        {
            decodedMessages ??= new Dictionary<Type, object?>();
            if (!decodedMessages.TryGetValue(endpoint.MessageType, out var message))
            {
                message = ZLinkEnvelopeCodec.DecodeBody(topicMessage.Parts, endpoint.MessageType);
                decodedMessages.Add(endpoint.MessageType, message);
            }

            var context = new ZLinkPublishContext(
                channelName,
                header.MessageName,
                header.ContentType,
                topicMessage.Topic,
                header.Source,
                cancellationToken);
            await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
