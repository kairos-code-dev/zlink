// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Native;
using Systems.Zlink.Runtime.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class SpotNode : ISpotNode
{
    public void SetPubBind(string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_pub_bind(_handle, endpoint);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetRouterBind(string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_router_bind(
            _handle, endpoint);
        ZlinkException.ThrowConfigIfError(rc);
    }

    /// <summary>
    /// Returns the resolved endpoint after bind (supports ephemeral port 0).
    /// </summary>
    public string LastEndpoint
    {
        get
        {
            return Status().LocalEndpoint;
        }
    }

    public void ConnectPeer(string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_peer(_handle,
            peerEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void ConnectPeerRid(RoutingId targetNodeRid, string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = targetNodeRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_connect_peer_rid(_handle,
            ref nativeRid, peerEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectPeer(string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_disconnect_peer(_handle,
            peerEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectPeerRid(RoutingId targetNodeRid)
    {
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = targetNodeRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_disconnect_peer_rid(_handle,
            ref nativeRid);
        ZlinkException.ThrowConnectIfError(rc);
    }

    /// <summary>
    /// Attaches this SPOT node to a discovery-owned service lifecycle.
    /// </summary>
    /// <remarks>
    /// Once attached, the discovery instance becomes the lifecycle owner for
    /// the node and is responsible for coordinated shutdown.
    /// </remarks>
    public void AttachDiscovery(Discovery discovery)
    {
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        int rc = NativeMethods.zlink_spot_node_attach_discovery(_handle,
            discovery.Handle);
        ZlinkException.ThrowConfigIfError(rc);
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
}
