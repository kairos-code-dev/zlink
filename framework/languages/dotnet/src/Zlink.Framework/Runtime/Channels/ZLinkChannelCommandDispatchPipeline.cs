using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Handlers;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelCommandDispatchPipeline(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    Func<string, IReadOnlySet<string>> resolveMappedGroups,
    LogLevel unhandledLogLevel,
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
            return;
        }

        var message = ZLinkEnvelopeCodec.DecodeBody(parts, endpoint.MessageType, codecs);
        var context = new ZLinkSendContext(
            channelName,
            header.MessageName,
            header.ContentType,
            cancellationToken);
        await dispatcher.DispatchAsync(endpoint, message, context, cancellationToken)
            .ConfigureAwait(false);
    }
}
