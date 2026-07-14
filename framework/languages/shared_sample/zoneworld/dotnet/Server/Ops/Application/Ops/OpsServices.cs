using ZoneWorld.Server.Ops.Ports;

namespace ZoneWorld.Server.Ops.Application.Ops;

/// <summary>
/// Publishes a world announcement (§8.2). The point of the fanout is what is *not* here:
/// Ops holds no node list, so adding a node changes nothing on this side. A SendToChannel
/// would have forced Ops to keep that list.
/// </summary>
public sealed class AnnouncementService
{
    private int _sequence;

    public string NextAnnouncementId() =>
        $"ann-{Interlocked.Increment(ref _sequence):D4}";
}

/// <summary>
/// Switches one node's maintenance mode (§8.4). The desired state is written first so it
/// survives a restart of the target node — the call itself may not even reach a node that
/// is currently down, and that is not an error.
/// </summary>
public sealed class MaintenanceService(IMaintenanceStorePort store)
{
    public ValueTask RecordDesiredAsync(string nodeId, bool enabled, CancellationToken cancellationToken) =>
        store.WriteAsync(nodeId, enabled, cancellationToken);
}
