using Zlink.Framework;

namespace Zlink.Framework.Backend.Contracts;

internal enum ZLinkBackendSpotDispatchEvent
{
    Internal = 0,
    RoutedReadable = 1,
    ChannelReplyReadable = 2,
}

internal readonly record struct ZLinkBackendSpotDispatchInfo(
    ZLinkBackendSpotDispatchEvent Event,
    IntPtr Subject);

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

    bool Request(
        Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
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

    bool Request(
        RoutingId routingId,
        Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags,
        TimeSpan? timeout);

    void Reply(
        RoutingId routingId,
        ulong requestSeq,
        Message message);
}

internal interface IZLinkBackendPublisherSocket : IZLinkBackendSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void OnSendReady(Action handler);

    bool Publish(
        string topic,
        Message message,
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
    void OnRawPacket(Func<string, Message, int> handler);

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
}

internal interface IZLinkBackendSpot : IZLinkBackendObject, IAsyncDisposable
{
    RoutingId RoutingId { get; }

    void SetRoutingId(RoutingId routingId);

    void SetSubscription(string topic);

    TopicMessage? Subscribe(RecvFlags flags);

    Received RecvRouted(RecvFlags flags);

    void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler);

    void OnSendReady(Action handler);

    void DrainChannelReplyFrom(IntPtr dealerSubject);

    bool RequestChannel(
        string channelName,
        Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags,
        TimeSpan? timeout);

    bool SendChannel(
        string channelName,
        Message message,
        SendFlags flags);

    bool Publish(
        string serviceName,
        string topic,
        Message message,
        SendFlags flags);

    bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        SendFlags flags);
}

internal interface IZLinkBackendSocketMonitor : IZLinkBackendObject, IAsyncDisposable
{
    void OnEvent(Action<ZLinkBackendSocketMonitorEvent> handler);

    ZLinkBackendSocketMonitorEvent Recv();
}
