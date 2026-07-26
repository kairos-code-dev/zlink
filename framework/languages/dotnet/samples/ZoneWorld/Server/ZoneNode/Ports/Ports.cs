namespace ZoneWorld.Server.ZoneNode.Ports;

/// <summary>
/// The desired maintenance state, owned by the operator and outlived by any node
/// restart (§8.4). ZLink does not provide a store for this: it is application state,
/// so the node reads it from the same Redis the location store uses.
/// </summary>
public interface IMaintenanceStorePort
{
    ValueTask<IReadOnlyDictionary<string, bool>> ReadAllAsync(CancellationToken cancellationToken);
}

/// <summary>
/// What this node tells the Ops console. Ops cannot subscribe to a remote node's spot
/// events — a spot event source only covers the SpotNode in the same process — so the
/// node observes them locally and reports them explicitly (§8.1).
/// </summary>
public interface IOpsReportPort
{
    ValueTask ReportSpotEventAsync(
        string kind,
        string detail,
        DateTimeOffset occurredAt,
        CancellationToken cancellationToken);

    ValueTask ReportNodeStatusAsync(
        IReadOnlyList<string> zones,
        int playerCount,
        bool maintenance,
        CancellationToken cancellationToken);
}
