using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Application.Node;
using ZoneWorld.Server.ZoneNode.Application.Zone;
using ZoneWorld.Server.ZoneNode.Ports;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Monitoring;

/// <summary>
/// Reports provider-neutral spot timer failures to Ops when they occur.
/// </summary>
/// <summary>Sends this node's reports to Ops over <c>zoneworld.report</c>.</summary>
internal sealed class OpsReportAdapter(
    IZLinkRouteClient channels,
    NodeMaintenancePolicy maintenance) : IOpsReportPort
{
    public async ValueTask ReportNodeStatusAsync(
        IReadOnlyList<string> zones,
        int playerCount,
        bool maintenanceEnabled,
        CancellationToken cancellationToken) =>
        await channels
            .SendToChannel(ZoneWorldNames.ReportChannel,
                new ReportNodeStatusMsg(
                    maintenance.OwnNodeId,
                    zones,
                    playerCount,
                    maintenanceEnabled))
            .Async(cancellationToken);
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
        stoppingToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "zone node status reporter ready. node={NodeId} mesh={MeshName}",
            maintenance.OwnNodeId,
            ZoneWorldNames.MeshName);
        using var timer = new PeriodicTimer(
            TimeSpan.FromMilliseconds(ZoneWorldSpec.NodeStatusReportPeriodMs));
        var firstReport = true;

        while (await timer.WaitForNextTickAsync(stoppingToken))
        {
            try
            {
                await ops.ReportNodeStatusAsync(
                    census.ZoneIds,
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
