using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Application.Node;
using ZoneWorld.Server.ZoneNode.Application.Zone;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Spots;
using ZoneWorld.Server.ZoneNode.Ports;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Actors;

/// <summary>
/// Brings the node up: restores the maintenance state the operator left behind, creates
/// the zone spots this node hosts, and spawns their bots.
///
/// The bots are why the world keeps moving with no client attached, and the four that
/// patrol across the X boundary keep a cross-node actor transfer happening continuously
/// (§2.7, ZW-F2).
/// </summary>
internal sealed class ZoneNodeBootstrap(
    IZLinkSpotManager spots,
    IZLinkActorDirectory directory,
    IZLinkActorClient actors,
    IMaintenanceStorePort store,
    NodeMaintenancePolicy maintenance,
    ZoneNodeSettings settings,
    ILogger<ZoneNodeBootstrap> logger) : IHostedService
{
    public async Task StartAsync(CancellationToken cancellationToken)
    {
        await RestoreMaintenanceAsync(cancellationToken);

        var zones = ZoneTopology.ZonesOf(maintenance.OwnNodeId);
        foreach (var zoneId in zones)
        {
            await spots.GetOrCreateAsync<ZoneSpot>(RoutingId.From(zoneId), cancellationToken);
            logger.LogInformation("zone hosted. node={NodeId}, zone={ZoneId}", maintenance.OwnNodeId, zoneId);
        }

        if (!settings.DisableBots)
            foreach (var route in zones.SelectMany(BotPatrolPolicy.RoutesOf))
                await SpawnBotAsync(route, cancellationToken);

        logger.LogInformation(
            "topology=ready node={NodeId} zones={Zones}",
            maintenance.OwnNodeId,
            string.Join(',', zones));
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    /// <summary>
    /// Reads the desired state for every node, not just this one. This node has to judge
    /// moves that leave for another node, so it needs that node's state too (§2.3). The
    /// fanout keeps it current from here on.
    /// </summary>
    private async Task RestoreMaintenanceAsync(CancellationToken cancellationToken)
    {
        var desired = await store.ReadAllAsync(cancellationToken);
        foreach (var (nodeId, enabled) in desired) maintenance.Apply(nodeId, enabled);

        logger.LogInformation(
            "maintenance restored. node={NodeId}, own={Own}, known={Known}",
            maintenance.OwnNodeId,
            maintenance.IsOwnNodeUnderMaintenance,
            desired.Count);
    }

    private async Task SpawnBotAsync(BotRoute route, CancellationToken cancellationToken)
    {
        // A bot outlives the node that spawned it. The X patrollers cross the boundary, so by
        // the time this node restarts one of "its" bots may be alive on the other node — and it
        // is still the same actor. Recreating it would be a second actor with the same id.
        //
        // The *directory* is what can answer that: it looks this node up first and then falls
        // back to the location store, so it sees the whole world. `IZLinkActorManager.FindAsync`
        // only knows actors this node created, so on a fresh restart it says "no" about a bot
        // that is very much alive next door.
        if (await directory.FindAsync(route.PlayerId, cancellationToken) is not null)
        {
            logger.LogInformation("bot already in the world, not respawned. bot={PlayerId}", route.PlayerId);
            return;
        }

        // Ensure, not create. If the create loses a claim race the directory looks again and
        // returns the winner; and if the actor genuinely cannot be placed — the location store
        // is unreachable, say — it throws. That distinction matters: swallowing the failure here
        // would let the node log topology=ready over a world with no bots in it (§2.7).
        var actorRef = await directory.EnsureAsync(
            route.PlayerId,
            ZLinkMessage.Empty,
            cancellationToken: cancellationToken);

        var entered = await actors
            .RequestToActor(
                actorRef,
                new EnterWorldReq(route.X, route.Y, IsBot: true, route.DirX, route.DirY))
            .Async<EnterWorldRes>(cancellationToken);

        logger.LogInformation(
            "bot spawned. bot={PlayerId}, zone={ZoneId}, dir=({DirX},{DirY})",
            route.PlayerId,
            entered.ZoneId,
            route.DirX,
            route.DirY);
    }
}
