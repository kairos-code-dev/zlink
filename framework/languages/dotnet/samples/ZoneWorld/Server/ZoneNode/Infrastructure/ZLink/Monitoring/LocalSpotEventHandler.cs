using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Eventing;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Application.Node;
using ZoneWorld.Server.ZoneNode.Application.Zone;
using ZoneWorld.Server.ZoneNode.Ports;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Monitoring;

/// <summary>
/// This node's own spot runtime events, forwarded to Ops (§8.1). Ops cannot subscribe to
/// them directly: a spot event source only covers the SpotNode in the same process. So
/// the node that owns the spots observes them and reports them explicitly.
/// </summary>
internal sealed class LocalSpotEventHandler(IOpsReportPort ops)
    : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
{
    public async ValueTask HandleAsync(ZLinkSpotEvent @event, CancellationToken cancellationToken)
    {
        switch (@event)
        {
            case ZLinkSpotEvent.TimerHandlerFailed failed:
                await ops.ReportSpotEventAsync(
                    NodeAlertKinds.TimerHandlerFailed,
                    $"timer={failed.Diagnostic.TimerName} spot={failed.Diagnostic.SpotRid}",
                    failed.Timestamp,
                    cancellationToken);
                break;

            case ZLinkSpotEvent.PeersChanged peers:
                await ops.ReportSpotEventAsync(
                    NodeAlertKinds.PeersChanged,
                    $"peers={peers.Peers.Count}",
                    peers.Timestamp,
                    cancellationToken);
                break;
        }

    }
}

/// <summary>Sends this node's reports to Ops over <c>zoneworld.report</c>.</summary>
internal sealed class OpsReportAdapter(
    IZLinkRouteClient channels,
    NodeMaintenancePolicy maintenance) : IOpsReportPort
{
    public async ValueTask ReportSpotEventAsync(
        string kind,
        string detail,
        DateTimeOffset occurredAt,
        CancellationToken cancellationToken) =>
        await channels
            .SendToChannel(ZoneWorldNames.MeshName, ZoneWorldNames.ReportChannel,
                new ReportSpotEventMsg(maintenance.OwnNodeId, kind, detail, occurredAt.ToString("O")))
            .Async(cancellationToken);

    public async ValueTask ReportNodeStatusAsync(
        string nodeRid,
        IReadOnlyList<string> zones,
        int playerCount,
        bool maintenanceEnabled,
        CancellationToken cancellationToken) =>
        await channels
            .SendToChannel(ZoneWorldNames.MeshName, ZoneWorldNames.ReportChannel,
                new ReportNodeStatusMsg(
                    maintenance.OwnNodeId,
                    nodeRid,
                    zones,
                    playerCount,
                    maintenanceEnabled))
            .Async(cancellationToken);
}

/// <summary>Reports this node's status every second so Ops can fill in PlayerCount (§8.1).</summary>
internal sealed class NodeStatusReporter(
    IOpsReportPort ops,
    IZLinkRouteMeshRuntime routeMesh,
    NodeMaintenancePolicy maintenance,
    NodePlayerCensus census,
    ILogger<NodeStatusReporter> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        stoppingToken.ThrowIfCancellationRequested();
        var nodeRid = routeMesh.Snapshot(ZoneWorldNames.MeshName).Rid.ToString();
        logger.LogInformation(
            "zone node mesh identity ready. node={NodeId} meshRid={MeshRid}",
            maintenance.OwnNodeId,
            nodeRid);
        using var timer = new PeriodicTimer(
            TimeSpan.FromMilliseconds(ZoneWorldSpec.NodeStatusReportPeriodMs));
        var firstReport = true;

        while (await timer.WaitForNextTickAsync(stoppingToken))
        {
            try
            {
                await ops.ReportNodeStatusAsync(
                    nodeRid,
                    ZoneTopology.ZonesOf(maintenance.OwnNodeId),
                    census.TotalPlayers,
                    maintenance.IsOwnNodeUnderMaintenance,
                    stoppingToken);
                if (firstReport)
                {
                    firstReport = false;
                    logger.LogInformation(
                        "node status report submitted. node={NodeId}",
                        maintenance.OwnNodeId);
                }
            }
            catch (Exception error)
            {
                logger.LogWarning(error, "node status report failed. node={NodeId}", maintenance.OwnNodeId);
            }
        }
    }
}
