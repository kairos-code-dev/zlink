namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkBackendSpotNode : IZLinkBackendObject, IAsyncDisposable
{
    RoutingId RoutingId { get; }

    void SetRoutingId(RoutingId routingId);

    void SetRouterBind(string endpoint);

    void SetPubBind(string endpoint);

    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void ConnectPeer(string endpoint);

    void ConnectPeer(RoutingId peerRid, string endpoint);

    void DisconnectPeer(string endpoint);

    IZLinkBackendSpot CreateSpot();

    IZLinkBackendSpot GetOrCreateSpot(RoutingId spotRid, out bool created);

    ZLinkSpotNodeStatus Status();

    IReadOnlyList<ZLinkSpotNodePeerEntry> Peers();

    IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects();

    IZLinkBackendSpotRouteBridge CreateRouteBridge();

    IZLinkBackendSpot EntrySpot();

    ZLinkBackendActorRef CreateActor(string actorId, Message createRequest);

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
        Message request,
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

    bool ForwardActorBoundSessionPart(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags);

    void BindRemoteActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid);

    ValueTask CloseActorBoundSessionAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken);
}

internal interface IZLinkBackendSpotRouteBridge : IZLinkBackendObject, IAsyncDisposable
{
    void AttachRouterChannel(
        string channelName,
        IZLinkBackendRouterSocket router,
        SpotRouteBridgeEndpointOptions? options = null);

    bool Send(
        string channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags);

    bool Request(
        string channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool HandleRouterReceived(
        string channelName,
        RoutingId sourceNodeRid,
        ulong requestSeq,
        IReadOnlyList<Message> parts);

    void Drain();
}

internal interface IZLinkBackendSpot : IZLinkBackendObject, IAsyncDisposable
{
    RoutingId RoutingId { get; }

    void SetRoutingId(RoutingId routingId);

    void SetSubscription(string topic);

    bool Subscribe(TopicMessage result, RecvFlags flags);

    bool RecvRoute(Received result, RecvFlags flags);

    void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler);

    void OnSendReady(Action handler);

    bool RequestToChannel(
        string channelName,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool RequestToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool SendToChannel(
        string channelName,
        Message message,
        SendFlags flags);

    bool SendToChannel(
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
