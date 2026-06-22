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

    public void ConnectRouterChannelPeer(string channelName, string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_router_channel_peer(
            _handle, channelName, endpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void ConnectRouterChannelPeerRid(
        string channelName,
        RoutingId peerRid,
        string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = peerRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_connect_router_channel_peer_rid(
            _handle, channelName, ref nativeRid, endpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectRouterChannelPeer(string channelName, string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_disconnect_router_channel_peer(
            _handle, channelName, endpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectRouterChannelPeerRid(string channelName,
        RoutingId peerRid)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = peerRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_disconnect_router_channel_peer_rid(
            _handle, channelName, ref nativeRid);
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

    public void AttachSpotRouteChannelDiscovery(string channelName,
        Discovery discovery)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        int rc = NativeMethods.zlink_spot_node_attach_router_channel_discovery(
            _handle, channelName, discovery.Handle);
        ZlinkException.ThrowConfigIfError(rc);
    }

    void ISpotNode.AttachSpotRouteChannelDiscovery(string channelName,
        IDiscovery discovery)
    {
        AttachSpotRouteChannelDiscovery(channelName,
            SocketInterop.RequireDiscovery(discovery, nameof(discovery)));
    }

    public void AttachChannelDealer(Discovery discovery, DealerSocket dealer)
    {
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        if (dealer == null)
            throw new ArgumentNullException(nameof(dealer));
        int rc = NativeMethods.zlink_spot_node_attach_channel_dealer(_handle,
            discovery.Handle, dealer.Handle);
        ZlinkException.ThrowConfigIfError(rc);
    }

    void ISpotNode.AttachChannelDealer(IDiscovery discovery,
        IDealerSocket dealer)
    {
        AttachChannelDealer(
            SocketInterop.RequireDiscovery(discovery, nameof(discovery)),
            SocketInterop.RequireDealerSocket(dealer, nameof(dealer)));
    }

    public void AttachChannelDealerManual(string channelName,
        DealerSocket dealer)
    {
        EnsureNotDisposed();
        if (dealer == null)
            throw new ArgumentNullException(nameof(dealer));
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        int rc = NativeMethods.zlink_spot_node_attach_channel_dealer_manual(
            _handle, channelName, dealer.Handle);
        ZlinkException.ThrowConfigIfError(rc);
        lock (_channelDealers)
            _channelDealers[channelName] = dealer;
    }

    void ISpotNode.AttachChannelDealerManual(string channelName,
        IDealerSocket dealer)
    {
        AttachChannelDealerManual(channelName,
            SocketInterop.RequireDealerSocket(dealer, nameof(dealer)));
    }

    public void AttachPubIngress(PubSocket pub)
    {
        EnsureNotDisposed();
        if (pub == null)
            throw new ArgumentNullException(nameof(pub));
        int rc = NativeMethods.zlink_spot_node_attach_pub_ingress(_handle,
            pub.Handle);
        ZlinkException.ThrowConfigIfError(rc);
    }

    void ISpotNode.AttachPubIngress(IPubSocket pub)
    {
        AttachPubIngress(SocketInterop.RequirePubSocket(pub, nameof(pub)));
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
