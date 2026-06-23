using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteSpotChannelCalls
{
    private readonly string _routerChannelId;
    private readonly ZLinkAsyncSubmitter _submitter;
    private readonly Func<IZLinkBackendSpotRouteBridge?> _getBridge;
    private readonly ZLinkMessageFlowTracer _flow;

    public ZLinkRouteSpotChannelCalls(
        IServiceProvider services,
        ZLinkFrameworkRegistration frameworkRegistration,
        string routerChannelId,
        ZLinkAsyncSubmitter submitter,
        Func<IZLinkBackendSpotRouteBridge?> getBridge)
    {
        _routerChannelId = routerChannelId;
        _submitter = submitter;
        _getBridge = getBridge;
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
        var bridge = RequireBridge();
        bridge.SetTargetNode(_routerChannelId, targetNodeRid);
        return _submitter.Async(
            parts,
            pending => bridge.Send(
                _routerChannelId,
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
        var bridge = RequireBridge();
        bridge.SetTargetNode(_routerChannelId, targetNodeRid);

        string? correlationId = null;
        string? packetName = null;
        if (_flow.Enabled(ZLinkMessageFlowPhase.Sent))
        {
            var header = ZLinkEnvelopeCodec.DecodeHeader(parts);
            correlationId = header.CorrelationId;
            packetName = header.MessageName;
            TraceSent(packetName, correlationId, targetNodeRid, targetSpotRid);
        }

        var reply = await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                parts,
                (pending, complete, fail) => bridge.Request(
                    _routerChannelId,
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

        if (_flow.Enabled(ZLinkMessageFlowPhase.ReplyReceived))
        {
            RecoverReplyTraceFields(reply, ref correlationId, ref packetName);
            _flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowPhase.ReplyReceived,
                ZLinkDispatchErrorSurface.SpotRoute,
                ZLinkDispatchMessageKind.Response,
                PacketName: packetName,
                ChannelName: _routerChannelId,
                CorrelationId: correlationId,
                SourceRid: targetNodeRid.ToString(),
                SpotRid: targetSpotRid.ToString()));
        }

        return reply;
    }

    private IZLinkBackendSpotRouteBridge RequireBridge()
    {
        return _getBridge()
            ?? throw new ZLinkConfigurationException(
                $"Route channel '{_routerChannelId}' is not attached to a SPOT route bridge.");
    }

    private void TraceSent(
        string? packetName,
        string? correlationId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid)
    {
        _flow.Trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowPhase.Sent,
            ZLinkDispatchErrorSurface.SpotRoute,
            ZLinkDispatchMessageKind.Request,
            PacketName: packetName,
            ChannelName: _routerChannelId,
            CorrelationId: correlationId,
            SourceRid: targetNodeRid.ToString(),
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
