namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkBackendSocket : IAsyncDisposable
{
    void Bind(string endpoint);

    void SetChannelName(string channelName);
}

internal interface IZLinkBackendSocketOptions : IZLinkBackendSocket
{
    void SetMaxMessageSize(long value);

    void SetSendHighWaterMark(int value);

    void SetReceiveHighWaterMark(int value);
}

internal interface IZLinkBackendConnectableSocket : IZLinkBackendSocket
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);
}

// ROUTER/DEALER serving socket 의 advertised peer weight 를 런타임에 읽고/쓴다(core PEER_WEIGHT).
internal interface IZLinkBackendWeightedSocket : IZLinkBackendSocket
{
    void SetPeerWeight(int weight);

    int GetPeerWeight();
}

internal interface IZLinkBackendDealerSocket : IZLinkBackendConnectableSocket, IZLinkBackendWeightedSocket,
    IZLinkBackendSocketOptions
{
    void SetRoutingId(RoutingId routingId);

    void SetProbe(bool enabled);

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

    Received? Recv(RecvFlags flags = RecvFlags.None);
}

internal interface IZLinkBackendRouterSocket : IZLinkBackendConnectableSocket, IZLinkBackendWeightedSocket,
    IZLinkBackendSocketOptions
{
    void OnSendReady(Action handler);

    void SetRoutingId(RoutingId routingId);

    /// <summary>Assigns the routing id of the peer the next outbound
    /// connect reaches, so rid-addressed sends work toward dialed peers.</summary>
    void SetConnectRoutingId(RoutingId routingId);

    void SetProbe(bool enabled);

    void SetMandatory(bool mandatory);

    void SetHandover(bool enabled);

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

internal interface IZLinkBackendPublisherSocket : IZLinkBackendSocket, IZLinkBackendSocketOptions
{
    void SetRoutingId(RoutingId routingId);

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

internal interface IZLinkBackendSubscriberSocket : IZLinkBackendConnectableSocket, IZLinkBackendSocketOptions
{
    void SetRoutingId(RoutingId routingId);

    void SetSubscription(string topic);

    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);
}

internal interface IZLinkBackendStreamSocket : IZLinkBackendSocket
{
    void SetTlsServer(string certPath, string keyPath, bool requireClientCert);

    void OnFramedPacket(Action<RoutingId, Message, Message> handler);

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

internal interface IZLinkBackendSocketMonitor : IAsyncDisposable
{
    void OnEvent(Action<ZLinkBackendSocketMonitorEvent> handler);

    bool TryRecv(out ZLinkBackendSocketMonitorEvent monitorEvent);
}
