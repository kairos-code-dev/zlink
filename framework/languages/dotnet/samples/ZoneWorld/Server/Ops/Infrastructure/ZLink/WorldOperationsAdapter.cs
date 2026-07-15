using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using ZoneWorld.Server.Ops.Ports;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Infrastructure.ZLink;

internal sealed class WorldOperationsAdapter(
    IZLinkFanoutClient fanout,
    IZLinkChannelClient channels,
    ILogger<WorldOperationsAdapter> logger) : IWorldOperationsPort
{
    public void PublishAnnouncement(string announcementId, string text) =>
        fanout
            .Publish(
                ZoneWorldNames.BroadcastChannel,
                ZoneWorldNames.AnnounceTopic,
                new WorldAnnounceEvent(announcementId, text))
            .Submit();

    public void PublishMaintenanceChange(string nodeId, bool enabled) =>
        fanout
            .Publish(
                ZoneWorldNames.BroadcastChannel,
                ZoneWorldNames.MaintenanceTopic,
                new NodeMaintenanceChangedEvent(nodeId, enabled))
            .Submit();

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
                .RequestToChannel(
                    ZoneWorldNames.OpsChannel(nodeId),
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
