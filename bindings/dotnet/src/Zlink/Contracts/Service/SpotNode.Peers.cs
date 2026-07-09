// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Manages SPOT node peer connections and route bridge attachment.
/// </summary>
public interface ISpotNodePeers
{
    /// <summary>
    ///     Connects to a peer endpoint.
    /// </summary>
    void ConnectPeer(string peerEndpoint);

    /// <summary>
    ///     Connects to a peer endpoint and associates it with a node routing id.
    /// </summary>
    void ConnectPeerRid(RoutingId targetNodeRid, string peerEndpoint);

    /// <summary>
    ///     Disconnects from a peer endpoint.
    /// </summary>
    void DisconnectPeer(string peerEndpoint);

    /// <summary>
    ///     Disconnects the peer identified by a node routing id.
    /// </summary>
    void DisconnectPeerRid(RoutingId targetNodeRid);

    /// <summary>
    ///     Creates a route bridge that borrows channel sockets owned by the caller.
    /// </summary>
    ISpotRouteBridge CreateRouteBridge(
        SpotRouteBridgeOptions? options = null);
}
