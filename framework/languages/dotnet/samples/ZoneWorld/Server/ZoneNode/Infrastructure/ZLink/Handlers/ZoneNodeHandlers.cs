using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Application.Node;
using ZoneWorld.Server.ZoneNode.Application.Zone;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Handlers;

/// <summary>
/// Ensures the player actor exists and reports where it landed (channel
/// <c>zoneworld.actors</c>). Only the node that hosts the spawn zone serves this channel,
/// because that node is the authority for the maintenance admission check (§2.3).
/// </summary>
[ZLinkHandlerGroup(HandlerGroups.ZoneActors)]
internal sealed class EnsurePlayerActorHandler(
    IZLinkActorManager actorManager,
    ILogger<EnsurePlayerActorHandler> logger)
    : IZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes>
{
    public async ValueTask<EnsurePlayerActorRes> HandleAsync(
        EnsurePlayerActorReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        // The actor is only created here. Entering the world is a separate step the Gateway
        // drives, and it has to come *after* the session is bound: the node hosting the
        // actor learns where to push when the binding reaches it, and the binding travels
        // with the join. Entering first would leave a player who never moves with no way to
        // be pushed to.
        var actorRef = await actorManager.GetOrCreateAsync(
            request.PlayerId,
            ZoneWorldNames.PlayerActorType,
            cancellationToken);

        logger.LogInformation("player actor ensured. player={PlayerId}", request.PlayerId);

        return new EnsurePlayerActorRes(
            request.PlayerId,
            new ActorRefWire(actorRef.NodeRid.ToString(), actorRef.ActorId, actorRef.Generation));
    }
}

/// <summary>
/// Switches this node's maintenance mode (owner-consistent channel
/// <c>zoneworld.ops.&lt;NodeId&gt;</c>). Ops calls the channel named after the node, so the
/// call reaches that node and no other — a plain client-server channel would spread it
/// across peers (§8.4).
/// </summary>
[ZLinkHandlerGroup(HandlerGroups.ZoneOps)]
internal sealed class ApplyNodeMaintenanceHandler(
    NodeMaintenancePolicy maintenance,
    ILogger<ApplyNodeMaintenanceHandler> logger)
    : IZLinkRequestHandler<ApplyNodeMaintenanceReq, ApplyNodeMaintenanceRes>
{
    public ValueTask<ApplyNodeMaintenanceRes> HandleAsync(
        ApplyNodeMaintenanceReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        maintenance.Apply(maintenance.OwnNodeId, request.Enabled);
        logger.LogInformation(
            "node maintenance applied. node={NodeId}, enabled={Enabled}",
            maintenance.OwnNodeId,
            request.Enabled);

        return ValueTask.FromResult(new ApplyNodeMaintenanceRes(
            maintenance.OwnNodeId,
            request.Enabled,
            ZoneTopology.ZonesOf(maintenance.OwnNodeId)));
    }
}

[ZLinkHandlerGroup(HandlerGroups.ZoneOps)]
internal sealed class GetNodeDiagnosticsHandler(
    NodeMaintenancePolicy maintenance,
    NodePlayerCensus census,
    ILogger<GetNodeDiagnosticsHandler> logger)
    : IZLinkRequestHandler<GetNodeDiagnosticsReq, GetNodeDiagnosticsRes>
{
    public ValueTask<GetNodeDiagnosticsRes> HandleAsync(
        GetNodeDiagnosticsReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation("node diagnostics requested. node={NodeId}", maintenance.OwnNodeId);
        return ValueTask.FromResult(new GetNodeDiagnosticsRes(
            maintenance.OwnNodeId,
            ZoneTopology.ZonesOf(maintenance.OwnNodeId),
            census.TotalPlayers,
            maintenance.IsOwnNodeUnderMaintenance));
    }
}
