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
    public ValueTask HandleAsync(ZLinkSpotEvent @event, CancellationToken cancellationToken)
    {
        switch (@event)
        {
            case ZLinkSpotEvent.TimerHandlerFailed failed:
                ops.ReportSpotEvent(
                    NodeAlertKinds.TimerHandlerFailed,
                    $"timer={failed.Diagnostic.TimerName} spot={failed.Diagnostic.SpotRid}",
                    failed.Timestamp);
                break;

            case ZLinkSpotEvent.PeersChanged peers:
                ops.ReportSpotEvent(
                    NodeAlertKinds.PeersChanged,
                    $"peers={peers.Peers.Count}",
                    peers.Timestamp);
                break;
        }

        return ValueTask.CompletedTask;
    }
}

/// <summary>Sends this node's reports to Ops over <c>zoneworld.report</c>.</summary>
internal sealed class OpsReportAdapter(
    IZLinkChannelClient channels,
    NodeMaintenancePolicy maintenance) : IOpsReportPort
{
    public void ReportSpotEvent(string kind, string detail, DateTimeOffset occurredAt) =>
        channels
            .SendToChannel(
                ZoneWorldNames.ReportChannel,
                new ReportSpotEventMsg(maintenance.OwnNodeId, kind, detail, occurredAt.ToString("O")))
            .Submit();

    public void ReportNodeStatus(IReadOnlyList<string> zones, int playerCount, bool maintenanceEnabled) =>
        channels
            .SendToChannel(
                ZoneWorldNames.ReportChannel,
                new ReportNodeStatusMsg(maintenance.OwnNodeId, zones, playerCount, maintenanceEnabled))
            .Submit();
}

/// <summary>Reports this node's status every second so Ops can fill in PlayerCount (§8.1).</summary>
internal sealed class NodeStatusReporter(
    IOpsReportPort ops,
    NodeMaintenancePolicy maintenance,
    NodePlayerCensus census,
    ILogger<NodeStatusReporter> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(
            TimeSpan.FromMilliseconds(ZoneWorldSpec.NodeStatusReportPeriodMs));

        while (await timer.WaitForNextTickAsync(stoppingToken))
        {
            try
            {
                ops.ReportNodeStatus(
                    ZoneTopology.ZonesOf(maintenance.OwnNodeId),
                    census.TotalPlayers,
                    maintenance.IsOwnNodeUnderMaintenance);
            }
            catch (Exception error)
            {
                logger.LogWarning(error, "node status report failed. node={NodeId}", maintenance.OwnNodeId);
            }
        }
    }
}
