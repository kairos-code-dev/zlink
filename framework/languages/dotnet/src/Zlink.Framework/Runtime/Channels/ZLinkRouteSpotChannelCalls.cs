using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteSpotChannelCalls
{
    private readonly string _routerChannelId;
    private readonly IZLinkBackendRouterSocket _router;
    private readonly ZLinkAsyncSubmitter _submitter;
    private readonly ZLinkMessageFlowTracer _flow;

    public ZLinkRouteSpotChannelCalls(
        IServiceProvider services,
        ZLinkFrameworkRegistration frameworkRegistration,
        string routerChannelId,
        IZLinkBackendRouterSocket router,
        ZLinkAsyncSubmitter submitter)
    {
        _routerChannelId = routerChannelId;
        _router = router;
        _submitter = submitter;
        _flow = new ZLinkMessageFlowTracer(
            frameworkRegistration.DispatchOptions,
            services,
            services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkRouteSpotChannelCalls>());
    }

    public ValueTask SubmitSendPartsAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _submitter.Async(
            parts,
            pending => _router.SendToSpot(
                targetNodeRid,
                targetSpotRid,
                pending,
                SendFlags.None),
            cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestPartsAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        string? correlationId = null;
        string? packetName = null;
        if (_flow.Enabled(ZLinkMessageFlowOutcome.Sent))
        {
            var header = ZLinkEnvelopeCodec.DecodeHeader(parts);
            correlationId = header.CorrelationId;
            packetName = header.MessageName;
            TraceSent(packetName, correlationId, targetNodeRid, targetSpotRid);
        }

        var reply = await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                parts,
                (pending, complete, fail) => _router.RequestToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    pending,
                    (result, reply) => ZLinkRawReplyCompletion.Complete(
                        result,
                        reply,
                        complete,
                        fail,
                        $"SPOT routed request failed with result '{result}'."),
                    SendFlags.None,
                    timeout),
                cancellationToken)
            .ConfigureAwait(false);

        if (_flow.Enabled(ZLinkMessageFlowOutcome.ReplyReceived))
        {
            RecoverReplyTraceFields(reply, ref correlationId, ref packetName);
            _flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.ReplyReceived,
                ZLinkDispatchErrorSurface.SpotRoute,
                ZLinkDispatchMessageKind.Response,
                PacketName: packetName,
                ChannelName: _routerChannelId,
                CorrelationId: correlationId,
                PeerRid: targetNodeRid.ToString(),
                SpotRid: targetSpotRid.ToString()));
        }

        return reply;
    }

    private void TraceSent(
        string? packetName,
        string? correlationId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid)
    {
        _flow.Trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.Sent,
            ZLinkDispatchErrorSurface.SpotRoute,
            ZLinkDispatchMessageKind.Request,
            PacketName: packetName,
            ChannelName: _routerChannelId,
            CorrelationId: correlationId,
            PeerRid: targetNodeRid.ToString(),
            SpotRid: targetSpotRid.ToString()));
    }

    private static void RecoverReplyTraceFields(
        IReadOnlyList<Message> reply,
        ref string? correlationId,
        ref string? packetName)
    {
        if (correlationId is not null || reply.Count == 0)
        {
            return;
        }

        try
        {
            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
            correlationId = replyHeader.CorrelationId;
            packetName ??= replyHeader.MessageName;
        }
        catch
        {
            // best-effort: trace without corr rather than fail the call.
        }
    }
}
