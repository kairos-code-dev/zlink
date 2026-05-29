// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Defines the spot node contract.
/// </summary>
public interface ISpotNode : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets or sets the router high water mark profile.
    /// </summary>
    AutoHwmProfile RouterHwmProfile { get; set; }
    /// <summary>
    /// Gets or sets the router high water mark.
    /// </summary>
    int RouterHighWaterMark { get; set; }
    /// <summary>
    /// Gets or sets the pub sub high water mark profile.
    /// </summary>
    AutoHwmProfile PubSubHwmProfile { get; set; }
    /// <summary>
    /// Gets or sets the pub sub high water mark.
    /// </summary>
    int PubSubHighWaterMark { get; set; }
    /// <summary>
    /// Sets whether publisher sends should avoid dropping messages.
    /// </summary>
    bool PublisherNoDrop { set; }
    /// <summary>
    /// Sets the publisher send timeout.
    /// </summary>
    TimeSpan? PublisherSendTimeout { set; }
    /// <summary>
    /// Gets or sets the dispatch workers min.
    /// </summary>
    int DispatchWorkersMin { get; set; }
    /// <summary>
    /// Gets or sets the dispatch workers max.
    /// </summary>
    int DispatchWorkersMax { get; set; }
    /// <summary>
    /// Gets the routing id.
    /// </summary>
    RoutingId RoutingId { get; }
    /// <summary>
    /// Gets the last endpoint.
    /// </summary>
    string LastEndpoint { get; }

    /// <summary>
    /// Sets the routing id.
    /// </summary>
    void SetRoutingId(RoutingId routingId);
    /// <summary>
    /// Sets the router bind.
    /// </summary>
    void SetRouterBind(string endpoint);
    /// <summary>
    /// Sets the pub bind.
    /// </summary>
    void SetPubBind(string endpoint);
    /// <summary>
    /// Connects to the endpoint.
    /// </summary>
    void ConnectPeer(string peerEndpoint);
    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void DisconnectPeer(string peerEndpoint);
    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void DisconnectPeerRid(RoutingId targetNodeRid);
    /// <summary>
    /// Connects to the endpoint.
    /// </summary>
    void ConnectRouterChannelPeer(string channelName, string endpoint);
    /// <summary>
    /// Connects to the endpoint.
    /// </summary>
    void ConnectRouterChannelPeerRid(
        string channelName,
        RoutingId peerRid,
        string endpoint);
    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void DisconnectRouterChannelPeer(string channelName, string endpoint);
    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void DisconnectRouterChannelPeerRid(string channelName, RoutingId peerRid);
    /// <summary>
    /// Attaches a discovery service.
    /// </summary>
    void AttachDiscovery(IDiscovery discovery);
    /// <summary>
    /// Attaches discovery for a spot route channel.
    /// </summary>
    void AttachSpotRouteChannelDiscovery(string channelName,
        IDiscovery discovery);
    /// <summary>
    /// Attaches a channel dealer socket.
    /// </summary>
    void AttachChannelDealer(IDiscovery discovery, IDealerSocket dealer);
    /// <summary>
    /// Attaches a manually named channel dealer socket.
    /// </summary>
    void AttachChannelDealerManual(string channelName, IDealerSocket dealer);
    /// <summary>
    /// Attaches a publisher ingress socket.
    /// </summary>
    void AttachPubIngress(IPubSocket pub);
    /// <summary>
    /// Creates a spot.
    /// </summary>
    ISpot CreateSpot();
    /// <summary>
    /// Gets the entry spot.
    /// </summary>
    ISpot EntrySpot();
    /// <summary>
    /// Gets the or create spot.
    /// </summary>
    ISpot GetOrCreateSpot(RoutingId spotRid, out bool created);
    /// <summary>
    /// Looks up a spot by routing id.
    /// </summary>
    ISpot? SpotLookup(RoutingId spotRid);
    /// <summary>
    /// Creates a actor.
    /// </summary>
    IActor CreateActor(string actorId);
    /// <summary>
    /// Looks up an actor by actor id.
    /// </summary>
    ActorRef ActorLookup(string actorId);
    /// <summary>
    /// Starts a send operation.
    /// </summary>
    SendOperation SendActorBoundSession(ActorRef actor);
    /// <summary>
    /// Closes the resource.
    /// </summary>
    void CloseActorBoundSession(ActorRef actor, TimeSpan timeout = default);
    /// <summary>
    /// Gets a remote actor reference.
    /// </summary>
    ActorLookupOperation RemoteActorGetRef(RoutingId targetNodeRid,
        string actorId);
    /// <summary>
    /// Starts an actor destroy operation.
    /// </summary>
    ActorDestroyOperation DestroyActor(ActorRef actor);
    /// <summary>
    /// Starts a join operation.
    /// </summary>
    ActorJoinOperation JoinActor(ActorRef actor, RoutingId destNodeRid,
        RoutingId destSpotRid);
    /// <summary>
    /// Starts a join operation.
    /// </summary>
    ActorJoinEntrySpotOperation JoinActorEntrySpot(ActorRef actor,
        RoutingId destNodeRid);
    /// <summary>
    /// Starts a leave operation.
    /// </summary>
    ActorLeaveOperation LeaveActor(ActorRef actor, RoutingId currentSpotRid);
    /// <summary>
    /// Sets the tls server.
    /// </summary>
    void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false);
    /// <summary>
    /// Sets the tls client.
    /// </summary>
    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);
    /// <summary>
    /// Gets the current status.
    /// </summary>
    SpotNodeStatus Status();
    /// <summary>
    /// Gets peer entries.
    /// </summary>
    SpotNodePeerEntry[] Peers();
    /// <summary>
    /// Gets peer entries matching a filter.
    /// </summary>
    SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter);
    /// <summary>
    /// Gets subject entries matching a filter.
    /// </summary>
    SpotNodeSubjectEntry[] Subjects(
        SpotNodeSubjectFilter? filter = null);
    /// <summary>
    /// Gets internal socket entries matching a filter.
    /// </summary>
    SpotNodeSocketEntry[] InternalSockets(
        SpotNodeSocketFilter? filter = null);
    /// <summary>
    /// Gets spot entries.
    /// </summary>
    SpotNodeSpotEntry[] Spots();
    /// <summary>
    /// Gets actor entries.
    /// </summary>
    SpotNodeActorEntry[] Actors();
    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}
