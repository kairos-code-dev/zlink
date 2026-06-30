// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class SpotNode : ISpotNode
{
    public void SetPubBind(string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_node_set_pub_bind(Handle, endpoint);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetRouterBind(string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_node_set_router_bind(
            Handle, endpoint);
        ZlinkException.ThrowConfigIfError(rc);
    }

    /// <summary>
    ///     Returns the resolved endpoint after bind (supports ephemeral port 0).
    /// </summary>
    public string LastEndpoint => Status().LocalEndpoint;

    public void ConnectPeer(string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_node_connect_peer(Handle,
            peerEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void ConnectPeerRid(RoutingId targetNodeRid, string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        var nativeRid = targetNodeRid.ToNative();
        var rc = NativeMethods.zlink_spot_node_connect_peer_rid(Handle,
            ref nativeRid, peerEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectPeer(string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_node_disconnect_peer(Handle,
            peerEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectPeerRid(RoutingId targetNodeRid)
    {
        EnsureNotDisposed();
        var nativeRid = targetNodeRid.ToNative();
        var rc = NativeMethods.zlink_spot_node_disconnect_peer_rid(Handle,
            ref nativeRid);
        ZlinkException.ThrowConnectIfError(rc);
    }

    void ISpotNode.AttachDiscovery(IDiscovery discovery)
    {
        AttachDiscovery(SocketInterop.RequireDiscovery(discovery,
            nameof(discovery)));
    }

    public ISpotRouteBridge CreateRouteBridge(
        SpotRouteBridgeOptions? options = null)
    {
        EnsureNotDisposed();
        return new SpotRouteBridge(this, options);
    }

    public ISpotNodePublisher CreatePublisher()
    {
        EnsureNotDisposed();
        return new SpotNodePublisher(this);
    }

    /// <summary>
    ///     Attaches this SPOT node to a discovery-owned service lifecycle.
    /// </summary>
    /// <remarks>
    ///     Once attached, the discovery instance becomes the lifecycle owner for
    ///     the node and is responsible for coordinated shutdown.
    /// </remarks>
    public void AttachDiscovery(Discovery discovery)
    {
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        var rc = NativeMethods.zlink_spot_node_attach_discovery(Handle,
            discovery.Handle);
        ZlinkException.ThrowConfigIfError(rc);
    }
}