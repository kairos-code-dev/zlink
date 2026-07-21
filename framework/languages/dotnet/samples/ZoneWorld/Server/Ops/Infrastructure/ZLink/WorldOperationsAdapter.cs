using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using ZoneWorld.Server.Ops.Ports;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Infrastructure.ZLink;

internal sealed class WorldOperationsAdapter(
    IZLinkFanoutClient fanout,
    IZLinkRouteClient channels,
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
            .SubmitAsync(cancellationToken);

    public async ValueTask PublishMaintenanceChangeAsync(
        string nodeId,
        bool enabled,
        CancellationToken cancellationToken) =>
        await fanout
            .Publish(
                ZoneWorldNames.BroadcastChannel,
                ZoneWorldNames.MaintenanceTopic,
                new NodeMaintenanceChangedEvent(nodeId, enabled))
            .SubmitAsync(cancellationToken);

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
    /// Owns the transport policy shared by owner-targeted operations: channel naming, timeout
    /// and the conversion from framework failures to the port's unavailable result.
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
            return await channels
                .RequestToChannel(ZoneWorldNames.MeshName, ZoneWorldNames.OpsChannel(nodeId),
                    request)
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
