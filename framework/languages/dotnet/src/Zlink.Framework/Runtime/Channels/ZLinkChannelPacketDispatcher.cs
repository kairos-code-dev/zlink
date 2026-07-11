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
    private readonly ZLinkFrameworkRuntime? _runtime;

    public ZLinkChannelPacketDispatcher(
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher,
        ZLinkFrameworkRegistration registration,
        ZLinkFrameworkRuntime? runtime,
        ILogger<ZLinkChannelPacketDispatcher>? logger = null)
    {
        _registration = registration;
        _runtime = runtime;
        var resolvedLogger = logger ?? NullLogger<ZLinkChannelPacketDispatcher>.Instance;
        _dispatchErrors = new ZLinkDispatchErrorReporter(
            registration.DispatchOptions,
            logger,
            runtime);
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

        ZLinkEnvelopeHeader header;
        try
        {
            header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
        }
        catch (ZLinkEnvelopeProtocolException protocolError)
        {
            HandleProtocolError(channelName, router, received, protocolError);
            return;
        }
        ZLinkFrameworkRuntime.ZLinkRuntimeOperationLease operation;
        if (_runtime is null)
            operation = new ZLinkFrameworkRuntime.ZLinkRuntimeOperationLease();
        else if (!_runtime.TryEnterInboundOperation(
                     header.Kind == ZLinkMessageKind.Request,
                     out operation))
        {
            if (header.Kind == ZLinkMessageKind.Request)
            {
                var error = new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestRejected,
                    "The framework runtime is draining and no longer accepts new requests.",
                    false);
                ZLinkChannelReplyWriter.ReplyRequest(
                    router,
                    received,
                    ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, error),
                    null,
                    null);
            }
            return;
        }
        using (operation)
        {
        using var currentFlow = ZLinkFlowContext.Enter(
            header.FlowId,
            header.FlowOrigin,
            _dispatchErrors.Flow.GenerationEnabled,
            ZLinkFlowOrigin.Inbound);
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
                        received.Parts,
                        header,
                        cancellationToken)
                    .ConfigureAwait(false);
                break;
        }
        }
    }

    public async Task DispatchEventMessageAsync(
        string channelName,
        TopicMessage topicMessage,
        CancellationToken cancellationToken)
    {
        if (topicMessage.Parts.Count == 0) return;

        ZLinkEnvelopeHeader header;
        try
        {
            header = ZLinkEnvelopeCodec.DecodeHeader(topicMessage.Parts);
        }
        catch (ZLinkEnvelopeProtocolException protocolError)
        {
            using var invalidFlow = ZLinkFlowContext.Enter(
                null,
                null,
                _dispatchErrors.Flow.GenerationEnabled,
                ZLinkFlowOrigin.Inbound);
            _dispatchErrors.Report(new ZLinkDispatchFailure(
                ZLinkDispatchErrorSurface.Channel,
                ZLinkDispatchMessageKind.Publish,
                ZLinkDispatchErrorReason.InvalidFrame,
                ZLinkDispatchErrorAction.Drop,
                protocolError.Header.MessageName,
                channelName,
                topicMessage.Topic,
                SourceRid: protocolError.Header.Source,
                CorrelationId: protocolError.Header.CorrelationId,
                Exception: protocolError));
            return;
        }
        ZLinkFrameworkRuntime.ZLinkRuntimeOperationLease operation;
        if (_runtime is null)
            operation = new ZLinkFrameworkRuntime.ZLinkRuntimeOperationLease();
        else if (!_runtime.TryEnterInboundOperation(countAsRequest: false, out operation))
            return;
        using (operation)
        {
        using var currentFlow = ZLinkFlowContext.Enter(
            header.FlowId,
            header.FlowOrigin,
            _dispatchErrors.Flow.GenerationEnabled,
            ZLinkFlowOrigin.Inbound);

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

    private void HandleProtocolError(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkEnvelopeProtocolException protocolError)
    {
        var header = protocolError.Header;
        using var flow = ZLinkFlowContext.Enter(
            null,
            null,
            _dispatchErrors.Flow.GenerationEnabled,
            ZLinkFlowOrigin.Inbound);
        _dispatchErrors.Report(new ZLinkDispatchFailure(
            ZLinkDispatchErrorSurface.Channel,
            header.Kind == ZLinkMessageKind.Request
                ? ZLinkDispatchMessageKind.Request
                : ZLinkDispatchMessageKind.Send,
            ZLinkDispatchErrorReason.InvalidFrame,
            header.Kind == ZLinkMessageKind.Request
                ? ZLinkDispatchErrorAction.ReplyError
                : ZLinkDispatchErrorAction.Drop,
            header.MessageName,
            channelName,
            CorrelationId: header.CorrelationId,
            Exception: protocolError));
        if (header.Kind != ZLinkMessageKind.Request) return;

        ZLinkChannelReplyWriter.ReplyRequest(
            router,
            received,
            ZLinkChannelReplyWriter.CreateProtocolErrorHeader(
                channelName,
                header,
                protocolError.Message),
            null,
            null);
    }

    private static IReadOnlySet<string> ResolveMappedGroups(
        ZLinkFrameworkRegistration registration,
        string channelName)
    {
        return registration.Channels.TryGetValue(channelName, out var channel)
            ? channel.HandlerGroups
            : EmptyGroups;
    }

}
