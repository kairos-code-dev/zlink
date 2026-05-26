namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkBackendSpotNode : IZLinkBackendObject, IAsyncDisposable
{
    RoutingId RoutingId { get; }

    void SetRoutingId(RoutingId routingId);

    void SetRouterBind(string endpoint);

    void SetPubBind(string endpoint);

    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void ConnectPeer(string endpoint);

    void DisconnectPeer(string endpoint);

    void ConnectRouterChannelPeer(string channelName, string endpoint);

    void ConnectRouterChannelPeerRid(
        string channelName,
        RoutingId peerRid,
        string endpoint);

    void DisconnectRouterChannelPeer(string channelName, string endpoint);

    void DisconnectRouterChannelPeerRid(string channelName, RoutingId peerRid);

    void AttachSpotRouteChannelDiscovery(string channelName, IZLinkBackendDiscovery discovery);

    IZLinkBackendSpot CreateSpot();

    IZLinkBackendSpot GetOrCreateSpot(RoutingId spotRid, out bool created);

    ZLinkSpotNodeStatus StatusSnapshot();

    IReadOnlyList<ZLinkSpotNodePeerEntry> PeersSnapshot();

    IReadOnlyList<ZLinkSpotNodeSubjectEntry> SubjectsSnapshot();

    void AttachChannelDealer(IZLinkBackendDiscovery discovery, IZLinkBackendDealerSocket dealer);

    void AttachChannelDealerManual(string channelName, IZLinkBackendDealerSocket dealer);

    IZLinkBackendSpot EntrySpot();

    ZLinkBackendActorRef CreateActor(string actorId);

    ZLinkBackendActorRef? ActorLookup(string actorId);

    bool JoinActor(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        RoutingId destSpotRid,
        Message message,
        RequestCallback callback,
        TimeSpan? timeout);

    bool JoinActor(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        ActorJoinCallback callback,
        TimeSpan? timeout);

    bool JoinActorEntrySpot(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        ActorJoinEntrySpotCallback callback,
        TimeSpan? timeout);

    ValueTask DestroyActorAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken);

    bool SendActorBoundSession(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags);

    ValueTask CloseActorBoundSessionAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken);
}

internal interface IZLinkBackendSpot : IZLinkBackendObject, IAsyncDisposable
{
    RoutingId RoutingId { get; }

    void SetRoutingId(RoutingId routingId);

    void SetSubscription(string topic);

    bool Subscribe(TopicMessage result, RecvFlags flags);

    bool RecvRoute(Received result, RecvFlags flags);

    void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler);

    void OnActorLifecycle(
        Action<ZLinkBackendSpotActorLifecycleInfo>? onJoin,
        Action<ZLinkBackendSpotActorLifecycleInfo>? onLeave);

    void OnSendReady(Action handler);

    bool RequestChannel(
        string channelName,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool RequestChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool SendChannel(
        string channelName,
        Message message,
        SendFlags flags);

    bool SendChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        SendFlags flags);

    bool Publish(
        string topic,
        Message message,
        SendFlags flags);

    bool Publish(
        string topic,
        IReadOnlyList<Message> parts,
        SendFlags flags);

    bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        SendFlags flags);

    bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags);

    bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags);

    void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        Message reply);

    void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        IReadOnlyList<Message> parts);
}
