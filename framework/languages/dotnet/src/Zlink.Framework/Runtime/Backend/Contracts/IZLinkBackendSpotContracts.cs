namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkBackendSpotNode : IAsyncDisposable
{
    RoutingId RoutingId { get; }

    void SetRoutingId(RoutingId routingId);

    void SetPublisherRoutingId(RoutingId routingId);

    void SetSubscriberRoutingId(RoutingId routingId);

    void SetRouterBind(string endpoint);

    void SetPubBind(string endpoint);

    // Adds a logical channel membership. Per spec 21-mesh-node §3 and the
    // IMeshNode contract, routing id + bind + at least one channel must be
    // configured before Start; call this before Start.
    void AddChannel(string channelName);

    // Sets a channel's load-balancing weight. Startup wiring calls this before
    // Start; the live runtime path (IZLinkRouteMeshRuntimeOptions, S8-02A) reuses
    // it after start (descriptor-revision bump, spec 21 §4).
    void SetChannelWeight(string channelName, uint weight);

    // Starts the node explicitly at the host-startup point after routing id,
    // bind and channels are applied (spec 21 §3). Idempotent.
    void Start();

    void ApplyRoleConfig(
        IZLinkSpotPublisherConfig? publisher,
        IZLinkSpotSubscriberConfig? subscriber);

    void OnSendReady(Action handler);

    void ConnectPeer(string endpoint);

    void ConnectPeer(RoutingId peerRid, string endpoint);

    void DisconnectPeer(string endpoint);

    // Retires an admitted peer lifetime by (RID, lifecycle generation). Core
    // queues a successor admission of the same RID behind this explicit
    // predecessor disconnect on every member that admitted the old lifetime.
    void DisconnectPeerLifetime(RoutingId peerRid, ulong lifecycleGeneration);

    IZLinkBackendSpot CreateSpot();

    IZLinkBackendSpot GetOrCreateSpot(RoutingId spotRid, out bool created);

    ZLinkSpotNodeStatus Status();

    IReadOnlyList<ZLinkSpotNodePeerEntry> Peers();

    // Raw MeshNode status/peer snapshots (rid, generations, admission state)
    // backing the IZLinkRouteMeshRuntime monitoring surface (spec 50 §2).
    MeshNodeStatus MeshStatus();

    IReadOnlyList<MeshNodePeer> MeshPeers();

    IMeshNodeMonitor OpenMeshMonitor(
        MeshMonitorEventMask events = MeshMonitorEventMask.All);

    IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects();

    IZLinkBackendSpot EntrySpot();

    // Node-addressed one-way send on the router plane (NodeSend record on the
    // target). Carries framework-internal packets such as the remote-session
    // push relay; the target's node route dispatcher decodes the envelope.
    SubmitResult SendToNode(RoutingId targetNodeRid, IReadOnlyList<Message> parts, SendFlags flags);

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

    bool SendToActor(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags);

    ValueTask<IReadOnlyList<Message>> RequestToActorAsync(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken);

    void ReplyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        IReadOnlyList<Message> parts);

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

    void CloseActorBoundSession(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken);

    // Native actor-transfer fence primitives (RouteMesh 10.0.0). Prepare fences
    // the actor and returns an opaque token; commit advances the membership epoch;
    // activate promotes the target; abort releases the fence. The distributed
    // authority that sequences these (S8-04A) is not implemented here.
    ZLinkBackendActorTransferToken PrepareActorTransfer(
        ZLinkBackendActorTransferPrepare prepare,
        out ZLinkBackendActorTransferPrepareResult result,
        TimeSpan timeout);

    void CommitActorTransfer(
        ZLinkBackendActorTransferToken token,
        ulong newMembershipEpoch);

    void ActivateActorTransfer(ZLinkBackendActorTransferToken token);

    void AbortActorTransfer(ZLinkBackendActorTransferToken token);

    // Registers the handler the node dispatch pump invokes for TransferControl
    // records so transfer phases drive the framework transfer state machine. The
    // orchestrating authority (S8-04A) registers the consumer; unset, the records
    // are delivered to no consumer instead of being silently dropped.
    void OnTransferControl(Action<ZLinkBackendActorTransferControl> handler);

    // Registers the handler the node dispatch pump invokes for node-addressed
    // (NodeSend/NodeRequest) and channel-addressed (ChannelSend/ChannelRequest)
    // records, so the MeshNode builder's registered route/channel handlers receive
    // inbound traffic. Requests reply through the record's held reply token.
    void OnNodeRoute(Action<ZLinkBackendRouteReceived> handler);
}

internal interface IZLinkBackendSpot : IAsyncDisposable
{
    RoutingId RoutingId { get; }

    // Core lifecycle generation of this spot activation — the value peers
    // must present on spot-addressed submits, published through the spot's
    // location row.
    ulong LifecycleGeneration { get; }

    void SetRoutingId(RoutingId routingId);

    void SetSubscription(string topic);

    ZLinkBackendSubscribeMessage? Subscribe(RecvFlags flags);

    ZLinkBackendRouteReceived? RecvRoute(RecvFlags flags);

    void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler);

    void OnSendReady(Action handler);

    //  Submit surfaces return the binding SubmitResult (not a flattened bool)
    //  so the exact call contract can report Backpressured, TargetNotFound and
    //  RouteNotConnected distinctly; `metadata` is the canonical application
    //  metadata frame (05-route-mesh §6), empty when the call set none.
    bool RequestToChannel(
        string channelName,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout,
        ReadOnlyMemory<byte> metadata);

    bool RequestToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout,
        ReadOnlyMemory<byte> metadata);

    SubmitResult SendToChannel(
        string channelName,
        Message message,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata);

    SubmitResult SendToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata);

    MeshPublishDetail Publish(
        string topic,
        Message message,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata);

    MeshPublishDetail Publish(
        string topic,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata);

    //  `spotGeneration` is the target Spot lifecycle generation from the
    //  resolved handle snapshot; the Core contract rejects 0 (03-spot §5).
    SubmitResult SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        ulong spotGeneration,
        Message message,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata);

    SubmitResult SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        ulong spotGeneration,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata);

    bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        ulong spotGeneration,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout,
        ReadOnlyMemory<byte> metadata);

    bool RequestToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        ulong spotGeneration,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout,
        ReadOnlyMemory<byte> metadata);

    ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags);

    ZLinkBackendSpotActorLifecycleEvent? RecvActorLifecycle(RecvFlags flags);

    void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        Message reply);

    void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        IReadOnlyList<Message> parts);
}
