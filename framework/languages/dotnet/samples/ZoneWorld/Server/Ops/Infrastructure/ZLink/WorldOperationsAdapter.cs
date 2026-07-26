using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using ZoneWorld.Server.Ops.Ports;
using ZoneWorld.Server.Ops.Application.Ops;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Infrastructure.ZLink;

internal sealed class WorldOperationsAdapter(
    IZLinkFanoutClient fanout,
    IZLinkRouteClient channels,
    NodeRegistry nodes,
    ILogger<WorldOperationsAdapter> logger) : IWorldOperationsPort
{
    public async ValueTask PublishAnnouncementAsync(
        string announcementId,
        string text,
        CancellationToken cancellationToken) =>
        await fanout
            .Publish(
                ZoneWorldNames.BroadcastChannel,
                ZoneWorldNames.AnnounceTopic,
                new WorldAnnounceEvent(announcementId, text))
            .Async(cancellationToken);

    public async ValueTask PublishMaintenanceChangeAsync(
        string nodeId,
        bool enabled,
        CancellationToken cancellationToken) =>
        await fanout
            .Publish(
                ZoneWorldNames.BroadcastChannel,
                ZoneWorldNames.MaintenanceTopic,
                new NodeMaintenanceChangedEvent(nodeId, enabled))
            .Async(cancellationToken);

    public ValueTask<ApplyNodeMaintenanceRes?> TryApplyMaintenanceAsync(
        string nodeId,
        bool enabled,
        CancellationToken cancellationToken) =>
        TryRequestNodeAsync<ApplyNodeMaintenanceRes>(
            nodeId,
            new ApplyNodeMaintenanceReq(nodeId, enabled),
            "maintenance",
            cancellationToken);

    public ValueTask<GetNodeDiagnosticsRes?> TryGetDiagnosticsAsync(
        string nodeId,
        CancellationToken cancellationToken) =>
        TryRequestNodeAsync<GetNodeDiagnosticsRes>(
            nodeId,
            new GetNodeDiagnosticsReq(nodeId),
            "diagnostics",
            cancellationToken);

    /// <summary>
    /// Owns the transport policy shared by node operations. The NodeRid is read from the
    /// current runtime observation and is not retained in an application request DTO.
    /// </summary>
    private async ValueTask<TResponse?> TryRequestNodeAsync<TResponse>(
        string nodeId,
        object request,
        string operation,
        CancellationToken cancellationToken)
        where TResponse : class
    {
        try
        {
            var targetNodeRid = nodes.RoutingIdOf(nodeId);
            if (targetNodeRid is null)
                return null;
            return await channels
                .RequestToNode(ZoneWorldNames.MeshName, targetNodeRid.Value, request)
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<TResponse>(cancellationToken);
        }
        catch (ZLinkFrameworkException error)
        {
            logger.LogInformation(
                "{Operation} target unavailable. node={NodeId}, error={Error}",
                operation,
                nodeId,
                error.Message);
            return null;
        }
    }
}
