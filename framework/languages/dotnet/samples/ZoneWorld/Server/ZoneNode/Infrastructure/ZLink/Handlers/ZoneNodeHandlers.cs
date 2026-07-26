using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Application.Node;
using ZoneWorld.Server.ZoneNode.Application.Zone;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Handlers;

/// <summary>
/// Switches this node's maintenance mode. Ops uses the NodeRid from its current runtime
/// snapshot for this immediate node-direct request.
/// </summary>
[ZLinkHandlerGroup(HandlerGroups.ZoneOps)]
internal sealed class ApplyNodeMaintenanceHandler(
    NodeMaintenancePolicy maintenance,
    NodePlayerCensus census,
    ILogger<ApplyNodeMaintenanceHandler> logger)
    : IZLinkRouteRequestHandler<ApplyNodeMaintenanceReq, ApplyNodeMaintenanceRes>
{
    public ValueTask<ApplyNodeMaintenanceRes> HandleAsync(
        ApplyNodeMaintenanceReq request,
        ZLinkRouteMessageContext context,
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
            census.ZoneIds));
    }
}

[ZLinkHandlerGroup(HandlerGroups.ZoneOps)]
internal sealed class GetNodeDiagnosticsHandler(
    NodeMaintenancePolicy maintenance,
    NodePlayerCensus census,
    ILogger<GetNodeDiagnosticsHandler> logger)
    : IZLinkRouteRequestHandler<GetNodeDiagnosticsReq, GetNodeDiagnosticsRes>
{
    public ValueTask<GetNodeDiagnosticsRes> HandleAsync(
        GetNodeDiagnosticsReq request,
        ZLinkRouteMessageContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation("node diagnostics requested. node={NodeId}", maintenance.OwnNodeId);
        return ValueTask.FromResult(new GetNodeDiagnosticsRes(
            maintenance.OwnNodeId,
            census.ZoneIds,
            census.TotalPlayers,
            maintenance.IsOwnNodeUnderMaintenance));
    }
}
