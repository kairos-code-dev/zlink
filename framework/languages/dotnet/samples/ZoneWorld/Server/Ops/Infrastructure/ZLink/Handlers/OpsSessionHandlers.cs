using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Streams;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.Ops.Application.Ops;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Sessions;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Infrastructure.ZLink.Handlers;

internal sealed class WatchNodesHandler(NodeRegistry nodes, OpsConsoleRegistry consoles)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, WatchNodesReq>
{
    public ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        WatchNodesReq request,
        CancellationToken cancellationToken)
    {
        context.Client.Reply(new WatchNodesRes(nodes.Snapshot())).Submit(cancellationToken);
        consoles.ReplayAlerts(context);
        return ValueTask.CompletedTask;
    }
}

/// <summary>
/// Publishes an announcement to every node without knowing how many there are (§8.2).
/// The fanout is the point: a third node can be started and it receives this with no
/// change to Ops.
/// </summary>
internal sealed class AnnounceWorldHandler(
    IZLinkFanoutClient fanout,
    AnnouncementService announcements,
    ILogger<AnnounceWorldHandler> logger)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, AnnounceWorldReq>
{
    public ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        AnnounceWorldReq request,
        CancellationToken cancellationToken)
    {
        var announcementId = announcements.NextAnnouncementId();

        fanout
            .Publish(
                ZoneWorldNames.BroadcastChannel,
                ZoneWorldNames.AnnounceTopic,
                new WorldAnnounceEvent(announcementId, request.Text))
            .Submit();

        logger.LogInformation(
            "announcement published. announcement={AnnouncementId}",
            announcementId);

        context.Client.Reply(new AnnounceWorldRes(announcementId)).Submit(cancellationToken);
        return ValueTask.CompletedTask;
    }
}

/// <summary>
/// Switches one node into maintenance (§8.4). The desired state is written first, so a
/// node that is currently down still picks the change up when it restarts. The call to the
/// node itself goes to the channel named after that node, so it reaches that node and no
/// other — a plain client-server channel would spread it across peers.
/// </summary>
internal sealed class SetMaintenanceHandler(
    IZLinkChannelClient channels,
    IZLinkFanoutClient fanout,
    MaintenanceService maintenance,
    ILogger<SetMaintenanceHandler> logger)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, SetMaintenanceReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        SetMaintenanceReq request,
        CancellationToken cancellationToken)
    {
        await maintenance.RecordDesiredAsync(request.NodeId, request.Enabled, cancellationToken);

        // Every node caches every node's maintenance state so it can judge a move that
        // leaves for another node (§2.3). The fanout is what keeps those caches current.
        fanout
            .Publish(
                ZoneWorldNames.BroadcastChannel,
                ZoneWorldNames.MaintenanceTopic,
                new NodeMaintenanceChangedEvent(request.NodeId, request.Enabled))
            .Submit();

        SetMaintenanceRes reply;
        try
        {
            var applied = await channels
                .RequestToChannel(
                    ZoneWorldNames.OpsChannel(request.NodeId),
                    new ApplyNodeMaintenanceReq(request.NodeId, request.Enabled))
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<ApplyNodeMaintenanceRes>(cancellationToken);

            reply = new SetMaintenanceRes(applied.NodeId, applied.Enabled, applied.Zones);
        }
        catch (ZLinkFrameworkException error)
        {
            // The node is not there. The desired state is already recorded, so it will
            // come up under maintenance when it restarts (§8.4).
            logger.LogInformation(
                "maintenance target unavailable. node={NodeId}, error={Error}",
                request.NodeId,
                error.Message);
            reply = new SetMaintenanceRes(
                request.NodeId,
                request.Enabled,
                ZoneTopology.ZonesOf(request.NodeId),
                ZoneWorldErrors.NodeUnavailable);
        }

        context.Client.Reply(reply).Submit(cancellationToken);
    }
}

internal sealed class NodeDiagnosticsHandler(
    IZLinkChannelClient channels,
    ILogger<NodeDiagnosticsHandler> logger)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, NodeDiagnosticsReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        NodeDiagnosticsReq request,
        CancellationToken cancellationToken)
    {
        NodeDiagnosticsRes reply;
        try
        {
            var diagnostics = await channels
                .RequestToChannel(
                    ZoneWorldNames.OpsChannel(request.NodeId),
                    new GetNodeDiagnosticsReq(request.NodeId))
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<GetNodeDiagnosticsRes>(cancellationToken);

            reply = new NodeDiagnosticsRes(
                diagnostics.NodeId,
                diagnostics.Zones,
                diagnostics.PlayerCount,
                diagnostics.Maintenance);
        }
        catch (ZLinkFrameworkException error)
        {
            logger.LogInformation(
                "diagnostics target unavailable. node={NodeId}, error={Error}",
                request.NodeId,
                error.Message);
            reply = new NodeDiagnosticsRes(
                request.NodeId,
                ZoneTopology.ZonesOf(request.NodeId),
                PlayerCount: 0,
                Maintenance: false,
                ZoneWorldErrors.NodeUnavailable);
        }

        context.Client.Reply(reply).Submit(cancellationToken);
    }
}
