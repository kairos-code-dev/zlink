using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelPacketDispatcher(
    ZLinkHandlerRegistry handlerRegistry,
    ZLinkHandlerDispatcher dispatcher,
    ZLinkFrameworkRegistration registration,
    ZLinkFrameworkRuntime runtime,
    ILogger<ZLinkChannelPacketDispatcher>? logger = null)
{
    private static readonly IReadOnlySet<string> EmptyGroups = new HashSet<string>(StringComparer.Ordinal);

    private readonly ZLinkChannelCommandDispatchPipeline _commandPipeline = new(
        handlerRegistry,
        dispatcher,
        channelName => ResolveMappedGroups(registration, channelName),
        registration.DispatchOptions.Unhandled.SendLogLevel,
        new ZLinkDispatchErrorReporter(
            registration.DispatchOptions,
            ResolveServices(runtime),
            logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance),
        registration.Codecs,
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance);

    private readonly ZLinkDispatchErrorReporter _dispatchErrors = new(
        registration.DispatchOptions,
        ResolveServices(runtime),
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance);

    private readonly ZLinkMessageFlowTracer _flow = new(
        registration.DispatchOptions,
        ResolveServices(runtime),
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance);

    private readonly ZLinkChannelPublishDispatchPipeline _publishPipeline = new(
        handlerRegistry,
        dispatcher,
        channelName => ResolveMappedGroups(registration, channelName),
        registration.DispatchOptions.Unhandled.PublishLogLevel,
        new ZLinkDispatchErrorReporter(
            registration.DispatchOptions,
            ResolveServices(runtime),
            logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance),
        registration.Codecs,
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance);

    private readonly ZLinkChannelRequestDispatchPipeline _requestPipeline = new(
        handlerRegistry,
        dispatcher,
        channelName => ResolveMappedGroups(registration, channelName),
        registration.Codecs,
        new ZLinkDispatchErrorReporter(
            registration.DispatchOptions,
            ResolveServices(runtime),
            logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance),
        logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance);

    public async Task DispatchServerMessageAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        CancellationToken cancellationToken)
    {
        if (received.Parts.Count == 0) return;

        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);

        if (_flow.Enabled(ZLinkMessageFlowOutcome.Received))
            _flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Received,
                ZLinkDispatchErrorSurface.Channel,
                header.Kind == ZLinkMessageKind.Request
                    ? ZLinkDispatchMessageKind.Request
                    : ZLinkDispatchMessageKind.Send,
                header.MessageName,
                channelName,
                CorrelationId: header.CorrelationId));

        switch (header.Kind)
        {
            case ZLinkMessageKind.Request:
                await HandleRequestAsync(channelName, router, received, header, cancellationToken)
                    .ConfigureAwait(false);
                break;
            case ZLinkMessageKind.Command:
                await _commandPipeline.DispatchAsync(
                        channelName,
                        "Channel",
                        received.Parts,
                        header,
                        cancellationToken)
                    .ConfigureAwait(false);
                break;
        }
    }

    public async Task DispatchEventMessageAsync(
        string channelName,
        TopicMessage topicMessage,
        CancellationToken cancellationToken)
    {
        if (topicMessage.Parts.Count == 0) return;

        var header = ZLinkEnvelopeCodec.DecodeHeader(topicMessage.Parts);

        if (_dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Received))
            _dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Received,
                ZLinkDispatchErrorSurface.Channel,
                ZLinkDispatchMessageKind.Publish,
                header.MessageName,
                channelName,
                topicMessage.Topic,
                SourceRid: header.Source,
                CorrelationId: header.CorrelationId));

        await _publishPipeline.DispatchAsync(
                channelName,
                topicMessage,
                header,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async Task HandleRequestAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        await _requestPipeline.DispatchAsync(
                channelName,
                "Channel",
                received,
                header,
                (replyHeader, reply, replyType) => ZLinkChannelReplyWriter.ReplyRequest(
                    router,
                    received,
                    replyHeader,
                    reply,
                    replyType,
                    registration.Codecs),
                errorHeader => ZLinkChannelReplyWriter.ReplyRequest(
                    router,
                    received,
                    errorHeader,
                    null,
                    null),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static IReadOnlySet<string> ResolveMappedGroups(
        ZLinkFrameworkRegistration registration,
        string channelName)
    {
        return registration.Channels.TryGetValue(channelName, out var channel)
            ? channel.HandlerGroups
            : EmptyGroups;
    }

    private static IServiceProvider ResolveServices(ZLinkFrameworkRuntime? runtime)
    {
        return runtime?.Services ?? EmptyServiceProvider.Instance;
    }
}