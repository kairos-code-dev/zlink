// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public interface ISpotNode : IDisposable, IAsyncDisposable
{
    AutoHwmProfile RouterHwmProfile { get; set; }
    int RouterHighWaterMark { get; set; }
    AutoHwmProfile PubSubHwmProfile { get; set; }
    int PubSubHighWaterMark { get; set; }
    bool PublisherNoDrop { set; }
    TimeSpan? PublisherSendTimeout { set; }
    int DispatchWorkersMin { get; set; }
    int DispatchWorkersMax { get; set; }
    RoutingId RoutingId { get; }
    string LastEndpoint { get; }

    void SetRoutingId(RoutingId routingId);
    void SetRouterBind(string endpoint);
    void SetPubBind(string endpoint);
    void ConnectPeer(string peerEndpoint);
    void DisconnectPeer(string peerEndpoint);
    void DisconnectPeerRid(RoutingId targetNodeRid);
    void ConnectRouterChannelPeer(string channelName, string endpoint);
    void ConnectRouterChannelPeerRid(
        string channelName,
        RoutingId peerRid,
        string endpoint);
    void DisconnectRouterChannelPeer(string channelName, string endpoint);
    void DisconnectRouterChannelPeerRid(string channelName, RoutingId peerRid);
    void AttachDiscovery(IDiscovery discovery);
    void AttachSpotRouteChannelDiscovery(string channelName,
        IDiscovery discovery);
    void AttachChannelDealer(IDiscovery discovery, IDealerSocket dealer);
    void AttachChannelDealerManual(string channelName, IDealerSocket dealer);
    void AttachPubIngress(IPubSocket pub);
    ISpot CreateSpot();
    ISpot EntrySpot();
    ISpot GetOrCreateSpot(RoutingId spotRid, out bool created);
    ISpot? SpotLookup(RoutingId spotRid);
    IActor CreateActor(string actorId);
    ActorRef ActorLookup(string actorId);
    SendOperation SendActorBoundSession(ActorRef actor);
    void CloseActorBoundSession(ActorRef actor, TimeSpan timeout = default);
    ActorLookupOperation RemoteActorGetRef(RoutingId targetNodeRid,
        string actorId);
    ActorDestroyOperation DestroyActor(ActorRef actor);
    ActorJoinOperation JoinActor(ActorRef actor, RoutingId destNodeRid,
        RoutingId destSpotRid);
    ActorJoinEntrySpotOperation JoinActorEntrySpot(ActorRef actor,
        RoutingId destNodeRid);
    ActorLeaveOperation LeaveActor(ActorRef actor, RoutingId currentSpotRid);
    void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false);
    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);
    SpotNodeStatus Status();
    SpotNodePeerEntry[] Peers();
    SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter);
    SpotNodeSubjectEntry[] Subjects(
        SpotNodeSubjectFilter? filter = null);
    SpotNodeSocketEntry[] InternalSockets(
        SpotNodeSocketFilter? filter = null);
    SpotNodeSpotEntry[] Spots();
    SpotNodeActorEntry[] Actors();
    void Close();
}
