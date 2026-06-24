// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// A spot node: hosts spots and actors, tunes their sockets, and exposes the
/// node's peers, subjects, and topology.
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
    /// Connects to the endpoint and uses the supplied node routing id for routed SPOT traffic.
    /// </summary>
    void ConnectPeerRid(RoutingId targetNodeRid, string peerEndpoint);
    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void DisconnectPeer(string peerEndpoint);
    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void DisconnectPeerRid(RoutingId targetNodeRid);
    /// <summary>
    /// Attaches a discovery service.
    /// </summary>
    void AttachDiscovery(IDiscovery discovery);
    /// <summary>
    /// Creates a route bridge that borrows channel sockets owned by the caller.
    /// </summary>
    ISpotRouteBridge CreateRouteBridge(
        SpotRouteBridgeOptions? options = null);
    /// <summary>
    /// Creates a publisher handle for the local SPOT topic plane.
    /// </summary>
    ISpotNodePublisher CreatePublisher();
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
    /// Creates an actor and delivers one request part to the entry spot create
    /// lifecycle callback.
    /// </summary>
    IActor CreateActor(string actorId, Message request);
    /// <summary>
    /// Creates an actor and delivers request parts to the entry spot create
    /// lifecycle callback.
    /// </summary>
    IActor CreateActor(string actorId, IReadOnlyList<Message> requestParts);
    /// <summary>
    /// Looks up an actor by actor id.
    /// </summary>
    ActorRef ActorLookup(string actorId);
    /// <summary>
    /// Begins a send to the bound session of <paramref name="actor"/>; parts
    /// are consumed on a successful submit (see <see cref="SendOperation"/>).
    /// </summary>
    SendOperation SendActorBoundSession(ActorRef actor);
    /// <summary>
    /// Forwards one stream session part to a remote actor while preserving the
    /// original session owner and session routing ids.
    /// </summary>
    bool ForwardActorBoundSessionPart(
        ActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags = SendFlags.None);
    /// <summary>
    /// Binds <paramref name="actor"/> to a session owned by a remote SPOT node.
    /// </summary>
    void BindRemoteActorBoundSession(
        ActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid);
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
        RoutingId destNodeRid, Message request);
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
