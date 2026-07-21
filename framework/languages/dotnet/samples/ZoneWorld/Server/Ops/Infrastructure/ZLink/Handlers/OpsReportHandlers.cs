using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using ZoneWorld.Server.Ops.Application.Ops;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Sessions;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Infrastructure.ZLink.Handlers;

/// <summary>
/// A spot event a zone node observed locally (§8.1). Ops cannot subscribe to it directly:
/// a spot event source only covers the SpotNode in the same process.
/// </summary>
[ZLinkHandlerGroup(HandlerGroups.Ops)]
internal sealed class ReportSpotEventHandler(
    OpsConsoleRegistry consoles,
    ILogger<ReportSpotEventHandler> logger)
    : IZLinkSendHandler<ReportSpotEventMsg>
{
    public async ValueTask HandleAsync(
        ReportSpotEventMsg message,
        ZLinkSendContext context,
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
    : IZLinkSendHandler<ReportNodeStatusMsg>
{
    public async ValueTask HandleAsync(
        ReportNodeStatusMsg message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        await nodes.ApplyReportAsync(message, cancellationToken);
    }
}
