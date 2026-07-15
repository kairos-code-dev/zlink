using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Ports;

/// <summary>
/// Operations that leave the Ops application boundary. Implementations hide fanout topics,
/// owner-consistent channels, timeouts and framework failure kinds from the use cases.
/// A null request result means that the selected node is currently unavailable.
/// </summary>
public interface IWorldOperationsPort
{
    void PublishAnnouncement(string announcementId, string text);

    void PublishMaintenanceChange(string nodeId, bool enabled);

    ValueTask<ApplyNodeMaintenanceRes?> TryApplyMaintenanceAsync(
        string nodeId,
        bool enabled,
        CancellationToken cancellationToken);

    ValueTask<GetNodeDiagnosticsRes?> TryGetDiagnosticsAsync(
        string nodeId,
        CancellationToken cancellationToken);
}
