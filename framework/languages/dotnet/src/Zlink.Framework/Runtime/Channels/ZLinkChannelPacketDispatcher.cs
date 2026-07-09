using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelPacketDispatcher
{
    private static readonly IReadOnlySet<string> EmptyGroups = new HashSet<string>(StringComparer.Ordinal);
    private readonly ZLinkChannelCommandDispatchPipeline _commandPipeline;
    private readonly ZLinkDispatchErrorReporter _dispatchErrors;
    private readonly ZLinkChannelPublishDispatchPipeline _publishPipeline;
    private readonly ZLinkChannelRequestDispatchPipeline _requestPipeline;
    private readonly ZLinkFrameworkRegistration _registration;

    public ZLinkChannelPacketDispatcher(
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher,
        ZLinkFrameworkRegistration registration,
        ZLinkFrameworkRuntime runtime,
        ILogger<ZLinkChannelPacketDispatcher>? logger = null)
    {
        _registration = registration;
        var resolvedLogger = logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance;
        _dispatchErrors = new ZLinkDispatchErrorReporter(
            registration.DispatchOptions,
            ResolveServices(runtime),
            resolvedLogger);
        _commandPipeline = new ZLinkChannelCommandDispatchPipeline(
            handlerRegistry,
            dispatcher,
            channelName => ResolveMappedGroups(registration, channelName),
            registration.DispatchOptions.Unhandled.SendLogLevel,
            _dispatchErrors,
            registration.Codecs,
            resolvedLogger);
        _publishPipeline = new ZLinkChannelPublishDispatchPipeline(
            handlerRegistry,
            dispatcher,
            channelName => ResolveMappedGroups(registration, channelName),
            registration.DispatchOptions.Unhandled.PublishLogLevel,
            _dispatchErrors,
            registration.Codecs,
            resolvedLogger);
        _requestPipeline = new ZLinkChannelRequestDispatchPipeline(
            handlerRegistry,
            dispatcher,
            channelName => ResolveMappedGroups(registration, channelName),
            registration.Codecs,
            _dispatchErrors,
            resolvedLogger);
    }

    public async Task DispatchServerMessageAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        CancellationToken cancellationToken)
    {
        if (received.Parts.Count == 0) return;

        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
        if (_dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Received))
            _dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
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
                    _registration.Codecs),
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
