using Systems.Zlink;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Locations;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.Ops.Application.Ops;
using ZoneWorld.Server.Ops.Infrastructure.ZLink.Sessions;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Ops.Infrastructure.ZLink.Monitoring;

/// <summary>
/// Whether a node is registered (§8.1). This is a change notification, not a question Ops
/// can ask: a node that has shut down is not there to answer, so the console learns about
/// it from the location runtime rather than by polling.
/// </summary>
internal sealed class LocationEventHandler(
    NodeRegistry nodes,
    ILogger<LocationEventHandler> logger)
    : IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>
{
    public async ValueTask HandleAsync(ZLinkLocationRuntimeEvent @event, CancellationToken cancellationToken)
    {
        if (@event is not ZLinkLocationRuntimeEvent.TopologyChanged topology)
            return;

        var live = topology.Topology
            .Where(entry => entry.MeshName == ZoneWorldNames.MeshName
                            && entry.State == ZLinkLocationTopologyState.Ready)
            .Select(entry => entry.NodeRid.ToString())
            .ToHashSet(StringComparer.Ordinal);

        await nodes.ApplyLiveRoutingIdsAsync(live, cancellationToken);

        logger.LogDebug(
            "location topology observed. entries={Count}, live={Live}",
            topology.Topology.Count,
            string.Join(',', live));
    }
}

/// <summary>
/// Whether a node's connection is up (§8.1). The node reports over the channel using its own
/// routing id, so a socket event on that channel names the node it came from.
/// </summary>
internal sealed class SocketEventHandler(
    NodeRegistry nodes,
    ILogger<SocketEventHandler> logger)
    : IZLinkRuntimeEventHandler<ZLinkMeshRuntimeEvent>
{
    public async ValueTask HandleAsync(ZLinkMeshRuntimeEvent @event, CancellationToken cancellationToken)
    {
        if (@event.PeerRid is not { } peer) return;

        var nodeId = nodes.NodeIdOf(peer.ToString());
        if (nodeId is null) return;

        var connected = @event.Reason switch
        {
            "ready" => true,
            "disconnected" => false,
            _ => (bool?)null
        };
        if (connected is null) return;

        await nodes.ApplyConnectionAsync(nodeId, connected.Value, cancellationToken);
        logger.LogInformation(
            "node connection observed. node={NodeId}, connected={Connected}, kind={Kind}",
            nodeId,
            connected.Value,
            @event.Reason);
    }
}

/// <summary>Pushes node state to the consoles as it changes. The console never polls
/// (§10.2), so every change has to leave through here.</summary>
internal sealed class NodeStatusBroadcaster(NodeRegistry nodes, OpsConsoleRegistry consoles) : IHostedService
{
    public Task StartAsync(CancellationToken cancellationToken)
    {
        nodes.Changed += Push;
        return Task.CompletedTask;
    }

    public Task StopAsync(CancellationToken cancellationToken)
    {
        nodes.Changed -= Push;
        return Task.CompletedTask;
    }

    private async ValueTask Push(NodeView node, CancellationToken cancellationToken) =>
        await consoles.BroadcastAsync(new NodeStatusNotify(
            node.NodeId,
            node.Registered,
            node.Connected,
            node.Maintenance,
            node.Zones,
            node.PlayerCount), cancellationToken);
}
