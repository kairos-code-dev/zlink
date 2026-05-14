using Zlink.Framework;

namespace Zlink.Framework.Backend.Contracts;

internal enum ZLinkBackendSpotDispatchEvent
{
    Internal = 0,
    RouteReadable = 1,
    ChannelReplyReadable = 2,
    ActorJoinReadable = 3,
    ActorReadable = 4,
}

internal readonly record struct ZLinkBackendActorRef(
    RoutingId NodeRid,
    string ActorId,
    ulong Generation);

internal readonly record struct ZLinkBackendSpotActorLifecycleInfo(
    ZLinkBackendActorRef? PreviousActor,
    ZLinkBackendActorRef? CurrentActor,
    RoutingId? PreviousSpotRid,
    RoutingId? CurrentSpotRid,
    ulong JoinEpoch,
    uint Flags);

internal sealed record ZLinkBackendActorPart(
    ZLinkBackendActorRef Actor,
    RoutingId SourceNodeRid,
    RoutingId SourceSessionRid,
    Message Message,
    bool More);

internal sealed record ZLinkBackendActorJoinRequest(
    ZLinkBackendActorRef SourceActor,
    ZLinkBackendActorRef TargetActor,
    RoutingId SourceNodeRid,
    RoutingId TargetSpotRid,
    ulong JoinEpoch,
    Message Message,
    IReadOnlyList<Message> Parts)
{
    internal object? NativeRequest { get; init; }
}

internal readonly record struct ZLinkBackendSpotDispatchInfo(
    ZLinkBackendSpotDispatchEvent Event,
    Action? DrainChannelReply = null,
    IReadOnlyList<ZLinkBackendActorPart>? ActorParts = null);

internal readonly record struct ZLinkBackendSocketMonitorEvent(
    ZLinkSocketNativeEventType NativeEvent,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr,
    uint Value);

internal interface IZLinkBackendObject
{
    object NativeInstance { get; }
}

internal interface IZLinkBackendContext : IZLinkBackendObject, IAsyncDisposable
{
}

internal interface IZLinkBackendDiscovery : IZLinkBackendObject, IAsyncDisposable
{
    void ConnectRegistry(string endpoint);

    IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers();
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

    TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None);
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

internal interface IZLinkBackendRegistry : IZLinkBackendObject, IAsyncDisposable
{
    void SetId(uint registryId);

    void SetHeartbeat(uint intervalMs, uint timeoutMs);

    void SetBroadcastInterval(uint intervalMs);

    void AddPeer(string endpoint);

    void Bind(string pubEndpoint, string routerEndpoint);

    ZLinkRegistryStatus StatusSnapshot();

    IReadOnlyList<ZLinkRegistryServiceSummaryEntry> ServiceSummarySnapshot(
        ZLinkRegistryServiceSummaryFilter? filter);

    IReadOnlyList<ZLinkRegistryTopologyEntry> TopologySnapshot();

    IReadOnlyList<ZLinkRegistryTopologyEntry> TopologyQuery(
        ZLinkRegistryTopologyFilter? filter);

    IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers(string channelName);
}

internal interface IZLinkBackendRegistryQueryClient : IZLinkBackendObject, IAsyncDisposable
{
    void Connect(string endpoint);

    IReadOnlyList<ZLinkRegistryTopologyEntry> Snapshot(
        ZLinkRegistryTopologyFilter? filter);
}

internal interface IZLinkBackendSpotNode : IZLinkBackendObject, IAsyncDisposable
{
    RoutingId RoutingId { get; }

    void SetRoutingId(RoutingId routingId);

    void Bind(string endpoint);

    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void ConnectPeer(string endpoint);

    void DisconnectPeer(string endpoint);

    IZLinkBackendSpot CreateSpot();

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
        RequestCallback callback,
        TimeSpan? timeout);

    ValueTask LeaveActorAsync(
        ZLinkBackendActorRef actor,
        RoutingId currentSpotRid,
        TimeSpan timeout,
        CancellationToken cancellationToken);

    ValueTask DestroyActorAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken);

}

internal interface IZLinkBackendSpot : IZLinkBackendObject, IAsyncDisposable
{
    RoutingId RoutingId { get; }

    void SetRoutingId(RoutingId routingId);

    void SetSubscription(string topic);

    TopicMessage? Subscribe(RecvFlags flags);

    Received RecvRoute(RecvFlags flags);

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
        bool accepted,
        Message reply);

    void ReplyActorJoin(
        ZLinkBackendActorJoinRequest request,
        bool accepted,
        IReadOnlyList<Message> parts);
}

internal interface IZLinkBackendSocketMonitor : IZLinkBackendObject, IAsyncDisposable
{
    void OnEvent(Action<ZLinkBackendSocketMonitorEvent> handler);

    ZLinkBackendSocketMonitorEvent Recv();
}
