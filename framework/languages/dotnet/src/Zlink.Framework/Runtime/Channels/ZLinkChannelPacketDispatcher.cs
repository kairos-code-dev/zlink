using Microsoft.Extensions.DependencyInjection;
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
            runtime is null
                ? logger
                : ZLinkMessageFlowTracer.CreateLogger(runtime.Services.GetService<ILoggerFactory>(), logger),
            runtime);
        _commandPipeline = new ZLinkChannelCommandDispatchPipeline(
            "legacy",
            handlerRegistry,
            dispatcher,
            channelName => ResolveMappedGroups(registration, channelName),
            LogLevel.Warning,
            _dispatchErrors,
            registration.Codecs,
            resolvedLogger);
        _publishPipeline = new ZLinkChannelPublishDispatchPipeline(
            "legacy",
            handlerRegistry,
            dispatcher,
            channelName => ResolveMappedGroups(registration, channelName),
            LogLevel.Debug,
            _dispatchErrors,
            registration.Codecs,
            resolvedLogger);
        _requestPipeline = new ZLinkChannelRequestDispatchPipeline(
            "legacy",
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
        if (received.Parts.Count == 0)
        {
            HandleProtocolError(channelName, router, received, ZLinkEnvelopeCodec.MissingHeader());
            return;
        }

        ZLinkEnvelopeHeader header;
        try
        {
            header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
            ZLinkEnvelopeCodec.ValidateDispatchHeader(header);
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
            _dispatchErrors.Flow.CaptureEnabled,
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
        if (topicMessage.Parts.Count == 0)
        {
            ReportPublishProtocolError(channelName, topicMessage, ZLinkEnvelopeCodec.MissingHeader());
            return;
        }

        ZLinkEnvelopeHeader header;
        try
        {
            header = ZLinkEnvelopeCodec.DecodeHeader(topicMessage.Parts);
            ZLinkEnvelopeCodec.ValidateDispatchHeader(header);
        }
        catch (ZLinkEnvelopeProtocolException protocolError)
        {
            ReportPublishProtocolError(channelName, topicMessage, protocolError);
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
            _dispatchErrors.Flow.CaptureEnabled,
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
                received.Parts,
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
        var isRequest = received.RequestSeq.HasValue;
        var canReply = isRequest && ZLinkEnvelopeCodec.CanCorrelateReply(header);
        var validFlow = ZLinkEnvelopeCodec.ValidFlow(header);
        using var flow = ZLinkFlowContext.Enter(
            validFlow.FlowId,
            validFlow.FlowOrigin,
            _dispatchErrors.Flow.CaptureEnabled,
            ZLinkFlowOrigin.Inbound);
        _dispatchErrors.Report(new ZLinkDispatchFailure(
            ZLinkDispatchErrorSurface.Channel,
            isRequest
                ? ZLinkDispatchMessageKind.Request
                : ZLinkDispatchMessageKind.Send,
            ZLinkDispatchErrorReason.InvalidFrame,
            canReply
                ? ZLinkDispatchErrorAction.ReplyError
                : ZLinkDispatchErrorAction.Drop,
            header.MessageName,
            channelName,
            CorrelationId: header.CorrelationId,
            Exception: protocolError));
        if (!canReply) return;

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

    private void ReportPublishProtocolError(
        string channelName,
        TopicMessage topicMessage,
        ZLinkEnvelopeProtocolException protocolError)
    {
        var validFlow = ZLinkEnvelopeCodec.ValidFlow(protocolError.Header);
        using var invalidFlow = ZLinkFlowContext.Enter(
            validFlow.FlowId,
            validFlow.FlowOrigin,
            _dispatchErrors.Flow.CaptureEnabled,
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
