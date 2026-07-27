using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using ZoneWorld.Server.Ops.Application.Ops;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Sessions;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Infrastructure.ZLink.Handlers;

/// <summary>
/// A provider-neutral spot timer failure event reported by a zone node (§8.1).
/// </summary>
[ZLinkHandlerGroup(HandlerGroups.Ops)]
internal sealed class ReportSpotEventHandler(
    OpsConsoleRegistry consoles,
    ILogger<ReportSpotEventHandler> logger)
    : IZLinkRouteSendHandler<ReportSpotEventMsg>
{
    public async ValueTask HandleAsync(
        ReportSpotEventMsg message,
        ZLinkRouteMessageContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "node alert. node={NodeId}, kind={Kind}, detail={Detail}",
            message.NodeId,
            message.Kind,
            message.Detail);

        var alert = new NodeAlertNotify(
            message.NodeId,
            message.Kind,
            message.Detail,
            message.OccurredAt);

        consoles.RecordAlert(alert);
        await consoles.BroadcastAsync(alert, cancellationToken);
    }
}

/// <summary>The one-second node report. It is where PlayerCount comes from: the runtime
/// events tell Ops that a node exists, not what it is holding (§8.1).</summary>
[ZLinkHandlerGroup(HandlerGroups.Ops)]
internal sealed class ReportNodeStatusHandler(NodeRegistry nodes)
    : IZLinkRouteSendHandler<ReportNodeStatusMsg>
{
    public async ValueTask HandleAsync(
        ReportNodeStatusMsg message,
        ZLinkRouteMessageContext context,
        CancellationToken cancellationToken)
    {
        await nodes.ApplyReportAsync(message, context.SourceNodeRid, cancellationToken);
    }
}
