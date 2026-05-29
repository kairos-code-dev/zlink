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
    /// Gets or sets the publisher no drop.
    /// </summary>
    bool PublisherNoDrop { set; }
    /// <summary>
    /// Gets or sets the publisher send timeout.
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
    /// Gets or sets the routing id.
    /// </summary>
    RoutingId RoutingId { get; }
    /// <summary>
    /// Gets or sets the last endpoint.
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
    /// Gets or sets the attach discovery.
    /// </summary>
    void AttachDiscovery(IDiscovery discovery);
    /// <summary>
    /// Gets or sets the attach spot route channel discovery.
    /// </summary>
    void AttachSpotRouteChannelDiscovery(string channelName,
        IDiscovery discovery);
    /// <summary>
    /// Gets or sets the attach channel dealer.
    /// </summary>
    void AttachChannelDealer(IDiscovery discovery, IDealerSocket dealer);
    /// <summary>
    /// Gets or sets the attach channel dealer manual.
    /// </summary>
    void AttachChannelDealerManual(string channelName, IDealerSocket dealer);
    /// <summary>
    /// Gets or sets the attach pub ingress.
    /// </summary>
    void AttachPubIngress(IPubSocket pub);
    /// <summary>
    /// Creates a spot.
    /// </summary>
    ISpot CreateSpot();
    /// <summary>
    /// Gets or sets the entry spot.
    /// </summary>
    ISpot EntrySpot();
    /// <summary>
    /// Gets the or create spot.
    /// </summary>
    ISpot GetOrCreateSpot(RoutingId spotRid, out bool created);
    /// <summary>
    /// Gets or sets the spot lookup.
    /// </summary>
    ISpot? SpotLookup(RoutingId spotRid);
    /// <summary>
    /// Creates a actor.
    /// </summary>
    IActor CreateActor(string actorId);
    /// <summary>
    /// Gets or sets the actor lookup.
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
    /// Gets or sets the remote actor get ref.
    /// </summary>
    ActorLookupOperation RemoteActorGetRef(RoutingId targetNodeRid,
        string actorId);
    /// <summary>
    /// Gets or sets the destroy actor.
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
    /// Gets or sets the peers.
    /// </summary>
    SpotNodePeerEntry[] Peers();
    /// <summary>
    /// Gets or sets the peers query.
    /// </summary>
    SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter);
    /// <summary>
    /// Gets or sets the subjects.
    /// </summary>
    SpotNodeSubjectEntry[] Subjects(
        SpotNodeSubjectFilter? filter = null);
    /// <summary>
    /// Gets or sets the internal sockets.
    /// </summary>
    SpotNodeSocketEntry[] InternalSockets(
        SpotNodeSocketFilter? filter = null);
    /// <summary>
    /// Gets or sets the spots.
    /// </summary>
    SpotNodeSpotEntry[] Spots();
    /// <summary>
    /// Gets or sets the actors.
    /// </summary>
    SpotNodeActorEntry[] Actors();
    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}
