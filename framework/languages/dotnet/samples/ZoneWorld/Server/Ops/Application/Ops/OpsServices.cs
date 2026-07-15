using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.Ops.Ports;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Application.Ops;

/// <summary>
/// Publishes a world announcement (§8.2) and owns its identity. The operations port hides the
/// fanout topic, so the use case has no node list and adding a node changes nothing here.
/// </summary>
public sealed class AnnouncementService(IWorldOperationsPort operations)
{
    private int _sequence;

    public string Publish(string text)
    {
        var announcementId = $"ann-{Interlocked.Increment(ref _sequence):D4}";
        operations.PublishAnnouncement(announcementId, text);
        return announcementId;
    }
}

/// <summary>
/// Switches one node's maintenance mode (§8.4). The desired state is written first so it
/// survives a restart of the target node — the call itself may not even reach a node that
/// is currently down. The response can report that the live application step was unavailable,
/// but the desired state itself is not lost.
/// </summary>
public sealed class MaintenanceService(
    IMaintenanceStorePort store,
    IWorldOperationsPort operations)
{
    public async ValueTask<SetMaintenanceRes> SetAsync(
        string nodeId,
        bool enabled,
        CancellationToken cancellationToken)
    {
        await store.WriteAsync(nodeId, enabled, cancellationToken);
        operations.PublishMaintenanceChange(nodeId, enabled);

        var applied = await operations.TryApplyMaintenanceAsync(nodeId, enabled, cancellationToken);
        return applied is null
            ? new SetMaintenanceRes(
                nodeId,
                enabled,
                ZoneTopology.ZonesOf(nodeId),
                ZoneWorldErrors.NodeUnavailable)
            : new SetMaintenanceRes(applied.NodeId, applied.Enabled, applied.Zones);
    }
}

/// <summary>Reads one node's live diagnostics without exposing channel or timeout behavior.</summary>
public sealed class NodeDiagnosticsService(IWorldOperationsPort operations)
{
    public async ValueTask<NodeDiagnosticsRes> GetAsync(
        string nodeId,
        CancellationToken cancellationToken)
    {
        var diagnostics = await operations.TryGetDiagnosticsAsync(nodeId, cancellationToken);
        return diagnostics is null
            ? new NodeDiagnosticsRes(
                nodeId,
                ZoneTopology.ZonesOf(nodeId),
                PlayerCount: 0,
                Maintenance: false,
                ZoneWorldErrors.NodeUnavailable)
            : new NodeDiagnosticsRes(
                diagnostics.NodeId,
                diagnostics.Zones,
                diagnostics.PlayerCount,
                diagnostics.Maintenance);
    }
}
