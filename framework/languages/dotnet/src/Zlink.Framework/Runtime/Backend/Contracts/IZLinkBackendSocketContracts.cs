namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkBackendDiscovery : IZLinkBackendObject, IAsyncDisposable
{
    bool SpotOwnerSyncEnabled { get; set; }

    bool ActorRouteSyncEnabled { get; set; }

    void ConnectRegistry(string endpoint);

    IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers();

    SpotRoute ResolveSpot(RoutingId spotRid);

    ActorRoute ResolveActor(string actorId);

    void BindRoute(uint kind, ReadOnlySpan<byte> key, ReadOnlySpan<byte> value);

    void UnbindRoute(uint kind, ReadOnlySpan<byte> key);

    ZLinkBackendDiscoveryRoute ResolveRoute(uint kind, ReadOnlySpan<byte> key);
}

internal interface IZLinkBackendSocket : IZLinkBackendObject, IAsyncDisposable
{
    void Bind(string endpoint);

    void SetChannelName(string channelName);
}

internal interface IZLinkBackendConnectableSocket : IZLinkBackendSocket
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);
}

internal interface IZLinkBackendDealerSocket : IZLinkBackendConnectableSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void OnSendReady(Action handler);

    bool Send(Message message, SendFlags flags);

    bool Send(IReadOnlyList<Message> parts, SendFlags flags);

    bool Request(
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool Request(
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);
}

internal interface IZLinkBackendRouterSocket : IZLinkBackendConnectableSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void OnSendReady(Action handler);

    void SetRoutingId(RoutingId routingId);

    Received? Recv(RecvFlags flags = RecvFlags.None);

    bool Send(
        RoutingId routingId,
        Message message,
        SendFlags flags);

    bool Send(
        RoutingId routingId,
        IReadOnlyList<Message> parts,
        SendFlags flags);

    bool Request(
        RoutingId routingId,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool Request(
        RoutingId routingId,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool SendToSpot(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags);

    bool RequestToSpot(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout);

    void Reply(
        RoutingId routingId,
        ulong requestSeq,
        Message message);

    void Reply(
        RoutingId routingId,
        ulong requestSeq,
        IReadOnlyList<Message> parts);
}

internal interface IZLinkBackendPublisherSocket : IZLinkBackendSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void OnSendReady(Action handler);

    bool Publish(
        string topic,
        Message message,
        SendFlags flags);

    bool Publish(
        string topic,
        IReadOnlyList<Message> parts,
        SendFlags flags);
}

internal interface IZLinkBackendSubscriberSocket : IZLinkBackendConnectableSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void SetSubscription(string topic);

    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);
}

internal interface IZLinkBackendStreamSocket : IZLinkBackendSocket
{
    void OnFramedPacket(Action<string, Message, Message> handler);

    bool Send(
        RoutingId routingId,
        Message payload,
        SendFlags flags);

    bool Send(
        RoutingId routingId,
        IReadOnlyList<Message> parts,
        SendFlags flags);

    void DisconnectPeer(RoutingId routingId);

    ValueTask BindActorAsync(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken);

    ValueTask UnbindActorAsync(
        RoutingId sessionRid,
        string actorId,
        TimeSpan timeout,
        CancellationToken cancellationToken);

    bool SendBoundActor(
        RoutingId sessionRid,
        string actorId,
        IReadOnlyList<Message> parts,
        SendFlags flags);
}

internal interface IZLinkBackendSocketMonitor : IZLinkBackendObject, IAsyncDisposable
{
    void OnEvent(Action<ZLinkBackendSocketMonitorEvent> handler);

    ZLinkBackendSocketMonitorEvent Recv();
}
