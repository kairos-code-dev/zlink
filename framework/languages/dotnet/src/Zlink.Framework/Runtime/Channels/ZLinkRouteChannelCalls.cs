using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteChannelCalls
{
    private readonly ZLinkCodecRegistryBuilder _codecs;
    private readonly ZLinkMessageFlowTracer _flow;
    private readonly IZLinkBackendRouterSocket _router;
    private readonly string _routerChannelId;
    private readonly ZLinkAsyncSubmitter _submitter;

    public ZLinkRouteChannelCalls(
        IServiceProvider services,
        ZLinkFrameworkRuntime? runtime,
        ZLinkFrameworkRegistration frameworkRegistration,
        string routerChannelId,
        IZLinkBackendRouterSocket router,
        ZLinkAsyncSubmitter submitter)
    {
        _routerChannelId = routerChannelId;
        _router = router;
        _submitter = submitter;
        _codecs = frameworkRegistration.Codecs;
        _flow = new ZLinkMessageFlowTracer(
            frameworkRegistration.DispatchOptions,
            ZLinkMessageFlowTracer.CreateLogger(services.GetService<ILoggerFactory>()),
            runtime,
            errorSink: runtime?.ErrorSink);
    }

    public ValueTask SubmitSendAsync<TMessage>(
        RoutingId targetNodeRid,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            _flow.CaptureEnabled);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            _routerChannelId,
            packetName);
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            header,
            message,
            message?.GetType() ?? typeof(TMessage),
            _codecs);

        TraceRouteSent(
            ZLinkDispatchMessageKind.Send,
            packetName,
            header.CorrelationId,
            targetNodeRid);

        return SubmitRouteSendPartsAsync(targetNodeRid, parts, cancellationToken);
    }

    public ValueTask SubmitSendPartsAsync(
        RoutingId targetNodeRid,
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> payloadParts,
        CancellationToken cancellationToken)
    {
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            _flow.CaptureEnabled);
        var parts = PrependHeader(header, payloadParts);

        TraceRouteSent(
            ZLinkDispatchMessageKind.Send,
            header.MessageName,
            header.CorrelationId,
            targetNodeRid);

        return SubmitRouteSendPartsAsync(targetNodeRid, parts, cancellationToken);
    }

    public async ValueTask<TReply> RequestAsync<TRequest, TReply>(
        RoutingId targetNodeRid,
        string packetName,
        TRequest request,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            _flow.CaptureEnabled);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            _routerChannelId,
            packetName,
            timeout);
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            header,
            request,
            request?.GetType() ?? typeof(TRequest),
            _codecs);

        TraceRouteSent(
            ZLinkDispatchMessageKind.Request,
            packetName,
            header.CorrelationId,
            targetNodeRid);

        var reply = await SubmitRouteRequestPartsAsync<TReply>(
                targetNodeRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);

        TraceRouteReplyReceived(
            packetName,
            header.CorrelationId,
            targetNodeRid);

        return reply;
    }

    public async ValueTask<TReply> RequestPartsAsync<TReply>(
        RoutingId targetNodeRid,
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> payloadParts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            _flow.CaptureEnabled);
        var parts = PrependHeader(header, payloadParts);

        TraceRouteSent(
            ZLinkDispatchMessageKind.Request,
            header.MessageName,
            header.CorrelationId,
            targetNodeRid);

        var reply = await SubmitRouteRequestPartsAsync<TReply>(
                targetNodeRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);

        TraceRouteReplyReceived(
            header.MessageName,
            header.CorrelationId,
            targetNodeRid);

        return reply;
    }

    private void TraceRouteSent(
        ZLinkDispatchMessageKind kind,
        string packetName,
        string? correlationId,
        RoutingId targetNodeRid)
    {
        if (!_flow.Enabled(ZLinkMessageFlowOutcome.Sent)) return;

        _flow.Trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.Sent,
            ZLinkDispatchErrorSurface.RouteMeshChannel,
            kind,
            packetName,
            _routerChannelId,
            CorrelationId: correlationId,
            SourceRid: targetNodeRid.ToString(),
            SocketRole: "router"));
    }

    private void TraceRouteReplyReceived(
        string packetName,
        string? correlationId,
        RoutingId targetNodeRid)
    {
        if (!_flow.Enabled(ZLinkMessageFlowOutcome.ReplyReceived)) return;

        _flow.Trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.ReplyReceived,
            ZLinkDispatchErrorSurface.RouteMeshChannel,
            ZLinkDispatchMessageKind.Response,
            packetName,
            _routerChannelId,
            CorrelationId: correlationId,
            SourceRid: targetNodeRid.ToString(),
            SocketRole: "router"));
    }

    private static IReadOnlyList<Message> PrependHeader(
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> payloadParts)
    {
        var parts = new Message[payloadParts.Count + 1];
        parts[0] = ZLinkEnvelopeCodec.EncodeHeader(header);
        for (var index = 0; index < payloadParts.Count; index++) parts[index + 1] = payloadParts[index];

        return parts;
    }

    private ValueTask SubmitRouteSendPartsAsync(
        RoutingId targetNodeRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _submitter.Async(
            parts,
            pending => _router.Send(targetNodeRid, pending, SendFlags.DontWait),
            cancellationToken);
    }

    private async ValueTask<TReply> SubmitRouteRequestPartsAsync<TReply>(
        RoutingId targetNodeRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);
        var metricStarted = ZLinkRuntimeMetrics.StartChannelRequest();
        var timedOut = false;
        try
        {
            return await _submitter
                .SubmitRequestAsync<TReply>(
                    parts,
                    (pending, complete, fail) => _router.Request(
                        targetNodeRid,
                        pending,
                        (result, reply) => ZLinkEnvelopeReplyCompletion.Complete(
                            result,
                            reply,
                            complete,
                            fail,
                            "ZLink routed request",
                            _codecs),
                        SendFlags.DontWait,
                        timeout),
                    timeoutSource.Token)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (timeoutSource.IsCancellationRequested
                                                 && !cancellationToken.IsCancellationRequested)
        {
            timedOut = true;
            throw new TimeoutException("ZLink routed request timed out.");
        }
        finally
        {
            ZLinkRuntimeMetrics.CompleteChannelRequest(metricStarted, timedOut);
        }
    }
}
