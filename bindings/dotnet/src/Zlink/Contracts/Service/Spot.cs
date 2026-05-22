// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading.Tasks;

namespace Systems.Zlink;

public enum SpotNodeMode
{
    PubSub = 1,
    Routed = 2,
    All = 3
}

public enum SpotNodeSocketOwner
{
    Any = 0,
    Node = 1,
    Spot = 2
}

public enum SpotNodeSocketType
{
    Any = 0,
    Pair = 0x1001,
    Pub = 0x1002,
    Sub = 0x1003,
    Dealer = 0x1004,
    Router = 0x1005,
    XPub = 0x1006,
    XSub = 0x1007,
    Stream = 0x1008
}

public sealed record SpotNodeSocketSnapshotFilter(
    SpotNodeSocketOwner? Owner = null,
    SpotNodeSocketType? SocketType = null,
    string? SocketName = null);

public sealed record SpotNodeSocketSnapshotEntry(
    SpotNodeSocketOwner Owner,
    ulong OwnerId,
    string OwnerName,
    string SocketName,
    SpotNodeSocketType SocketType,
    bool AutoHwmVisible,
    MonitorSnapshot Snapshot);

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
    void SetRouterBindEndpoint(string endpoint);
    void Bind(string endpoint);
    void ConnectPeer(string peerEndpoint);
    void DisconnectPeer(string peerEndpoint);
    void DisconnectPeerRid(RoutingId targetNodeRid);
    void ConnectRouterChannelPeer(string channelName, string endpoint);
    void DisconnectRouterChannelPeer(string channelName, string endpoint);
    void DisconnectRouterChannelPeerRid(string channelName, RoutingId peerRid);
    void AttachDiscovery(IDiscovery discovery);
    void AttachSpotRouteChannelDiscovery(string channelName, IDiscovery discovery);
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
    ActorLookupOperation RemoteActorGetRef(RoutingId targetNodeRid, string actorId);
    ActorDestroyOperation DestroyActor(ActorRef actor);
    ActorJoinOperation JoinActor(ActorRef actor, RoutingId destNodeRid, RoutingId destSpotRid);
    ActorLeaveOperation LeaveActor(ActorRef actor, RoutingId currentSpotRid);
    void SetTlsServer(string certPath, string keyPath, bool requireClientCert = false);
    void SetTlsClient(string caCertPath, string hostname, bool trustSystem = false);
    SpotNodeStatus StatusSnapshot();
    SpotNodePeerEntry[] PeersSnapshot();
    SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter);
    SpotNodeSubjectEntry[] SubjectsSnapshot(SpotNodeSubjectFilter? filter = null);
    SpotNodeSocketSnapshotEntry[] InternalSocketsSnapshot(SpotNodeSocketSnapshotFilter? filter = null);
    SpotNodeSpotEntry[] SpotsSnapshot();
    SpotNodeActorEntry[] ActorsSnapshot();
    void Close();
}

public interface ISpot : IZlinkSocket, IDisposable, IAsyncDisposable
{
    RoutingId RoutingId { get; }
    TimeSpan? RequestTimeout { get; set; }
    int SendHighWaterMark { get; set; }
    int ReceiveHighWaterMark { get; set; }
    int SendBufferSize { get; set; }
    int ReceiveBufferSize { get; set; }
    TimeSpan? SendTimeout { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    TimeSpan? Linger { get; set; }

    void SetRoutingId(RoutingId routingId);
    SendOperation Publish(string topic);
    bool Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    SendOperation SendChannel(string channelName);
    RequestOperation RequestChannel(string channelName);
    SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, SendFlags flags = SendFlags.None);
    RequestOperation RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    RequestOperation RequestToRouter(RoutingId peerRid);
    ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid, ulong requestSeq);
    ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq);
    void SetSubscription(string topicOrPattern);
    void UnsetSubscription(string topicOrPattern);
    SubscriptionEntry? SubscriptionAt(int index);
    bool SubscribePart(Message result, Span<byte> topicBuffer,
        out int topicLength, out bool hasMore,
        RecvFlags flags = RecvFlags.None);
    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);
    bool ReceiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags = RecvFlags.None);
    void OnSendReady(Action handler);
    bool RecvRoutedPart(Message result, out RoutingId? routingId,
        out RoutingId? spotRid, out ulong? requestSeq, out bool hasMore,
        RecvFlags flags = RecvFlags.None);
    bool RecvRouted(Received result, RecvFlags flags = RecvFlags.None);
    ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None);
    ActorJoinReplyOperation ReplyActorJoin(ActorJoinRequest request, bool accepted);
    ActorRef[] ActorsSnapshot();
    void OnRoutedReceive(Action<Received> handler);
    void OnDispatchEvent(Action<SpotDispatchInfo> handler);
    void OnActorLifecycle(Action<SpotActorLifecycleInfo>? onJoin, Action<SpotActorLifecycleInfo>? onLeave);
    void Close();
}
