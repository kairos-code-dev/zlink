using System.Text;
using Systems.Zlink;
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
    public ValueTask HandleAsync(ZLinkLocationRuntimeEvent @event, CancellationToken cancellationToken)
    {
        if (@event is not ZLinkLocationRuntimeEvent.TopologyChanged topology)
            return ValueTask.CompletedTask;

        var live = topology.Topology
            .Where(entry => entry.MeshName == ZoneWorldNames.ZoneMesh && entry.NodeRid is not null)
            .Select(entry => entry.NodeRid!.Value.ToString())
            .ToHashSet(StringComparer.Ordinal);

        foreach (var nodeId in ZoneTopology.ZoneNodes)
            nodes.ApplyRegistration(nodeId, live.Contains(ZoneTopology.RidOf(nodeId)));

        logger.LogDebug(
            "location topology observed. entries={Count}, live={Live}",
            topology.Topology.Count,
            string.Join(',', live));
        return ValueTask.CompletedTask;
    }
}

/// <summary>
/// Whether a node's connection is up (§8.1). The node reports over the channel using its own
/// routing id, so a socket event on that channel names the node it came from.
/// </summary>
internal sealed class SocketEventHandler(
    NodeRegistry nodes,
    ILogger<SocketEventHandler> logger)
    : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        if (@event.RoutingId is not { } peer) return ValueTask.CompletedTask;

        var nodeId = ZoneTopology.NodeOfRid(BaseRidOf(peer));
        if (nodeId is null) return ValueTask.CompletedTask;

        var connected = @event.Event switch
        {
            ZLinkSocketEventKind.Connected or ZLinkSocketEventKind.ConnectionReady => true,
            ZLinkSocketEventKind.Disconnected or ZLinkSocketEventKind.Closed => false,
            _ => (bool?)null
        };
        if (connected is null) return ValueTask.CompletedTask;

        nodes.ApplyConnection(nodeId, connected.Value);
        logger.LogInformation(
            "node connection observed. node={NodeId}, connected={Connected}, kind={Kind}",
            nodeId,
            connected.Value,
            @event.Event);
        return ValueTask.CompletedTask;
    }

    /// <summary>
    /// A channel's client and server sockets share the node's routing id, so the framework tells
    /// them apart by deriving one as <c>&lt;rid&gt;\0&lt;role&gt;</c>. The socket event carries the
    /// derived id; the node is named by the part before the separator.
    /// </summary>
    private static string BaseRidOf(RoutingId peer)
    {
        ReadOnlySpan<byte> bytes = peer.ToBytes();
        var end = bytes.IndexOf((byte)0);
        return Encoding.UTF8.GetString(end < 0 ? bytes : bytes[..end]);
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

    private void Push(NodeView node) =>
        consoles.Broadcast(new NodeStatusNotify(
            node.NodeId,
            node.Registered,
            node.Connected,
            node.Maintenance,
            node.Zones,
            node.PlayerCount));
}
