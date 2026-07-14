using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Application.Node;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Handlers;

/// <summary>
/// The world announcement (§8.2). Ops publishes it to a fanout channel without holding a
/// node list, so adding a node changes nothing on the publishing side.
///
/// The announcement is then handed to this node's own zone spots with a send, not a spot
/// publish: a publish reaches the whole mesh, so every zone spot would receive the
/// announcement once per node. The node knows its own zones from configuration, so it
/// addresses exactly those, through the spot bridge.
/// </summary>
internal sealed class WorldAnnounceSubscriber(
    IZLinkRouteClient routes,
    IZLinkSpotHandleResolver spotHandles,
    NodeMaintenancePolicy maintenance,
    ILogger<WorldAnnounceSubscriber> logger)
    : IZLinkPublishHandler<WorldAnnounceEvent>
{
    public async ValueTask HandleAsync(
        WorldAnnounceEvent message,
        ZLinkPublishContext context,
        CancellationToken cancellationToken)
    {
        var zones = ZoneTopology.ZonesOf(maintenance.OwnNodeId);
        logger.LogInformation(
            "fanout subscriber received announcement. node={NodeId}, announcement={AnnouncementId}, zones={ZoneCount}",
            maintenance.OwnNodeId,
            message.AnnouncementId,
            zones.Count);

        foreach (var zoneId in zones)
        {
            // One zone that cannot be reached must not take the others down with it, and it must
            // not pass unnoticed either: an announcement that silently reaches half the world
            // looks exactly like one that reached all of it.
            try
            {
                var handle = await spotHandles.ResolveSpotHandleAsync(
                    RoutingId.From(zoneId),
                    cancellationToken);
                if (handle is null)
                {
                    logger.LogError(
                        "announcement dropped: the node's own zone spot did not resolve. zone={ZoneId}",
                        zoneId);
                    continue;
                }

                routes
                    .SendToSpot(handle, new DeliverAnnounceMsg(message.AnnouncementId, message.Text))
                    .Submit(cancellationToken);
            }
            catch (Exception error)
            {
                logger.LogError(
                    error,
                    "announcement dropped: delivering to the node's own zone spot failed. zone={ZoneId}",
                    zoneId);
            }
        }
    }
}

/// <summary>
/// Keeps this node's view of every node's maintenance state current (§2.3). The node
/// judging a move is the one the player is leaving, so it needs the target node's state;
/// reading the store on every move would be expensive, so the state arrives by fanout.
/// </summary>
internal sealed class NodeMaintenanceChangedSubscriber(
    NodeMaintenancePolicy maintenance,
    ILogger<NodeMaintenanceChangedSubscriber> logger)
    : IZLinkPublishHandler<NodeMaintenanceChangedEvent>
{
    public ValueTask HandleAsync(
        NodeMaintenanceChangedEvent message,
        ZLinkPublishContext context,
        CancellationToken cancellationToken)
    {
        maintenance.Apply(message.NodeId, message.Enabled);
        logger.LogInformation(
            "maintenance cache updated. observer={ObserverNodeId}, node={NodeId}, enabled={Enabled}",
            maintenance.OwnNodeId,
            message.NodeId,
            message.Enabled);
        return ValueTask.CompletedTask;
    }
}
