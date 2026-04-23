using Zlink.Framework;

namespace Zlink.Framework.Backend;

internal enum ZLinkBackendServiceType
{
    Socket = 1,
    Spot = 2,
}

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
    global::Zlink.RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr,
    uint Value);

internal readonly record struct ZLinkBackendDiscoveryMonitorEvent(
    ZLinkDiscoveryNativeEventType NativeEvent,
    string ServiceName,
    string Endpoint,
    global::Zlink.RoutingId? RoutingId,
    string Subject,
    ZLinkSubjectKind SubjectKind,
    uint Status,
    uint ErrorCode,
    ulong Value,
    uint DetailFlags);

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

    bool Send(global::Zlink.Message message, global::Zlink.SendFlags flags);

    ValueTask<IReadOnlyList<global::Zlink.Message>> RequestAsync(
        global::Zlink.Message message,
        TimeSpan timeout,
        CancellationToken cancellationToken);
}

internal interface IZLinkBackendRouterSocket : IZLinkBackendSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    global::Zlink.Received? Recv();

    void Reply(
        global::Zlink.RoutingId routingId,
        ulong requestSeq,
        global::Zlink.Message message);
}

internal interface IZLinkBackendPublisherSocket : IZLinkBackendSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    bool Publish(
        string topic,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags);
}

internal interface IZLinkBackendSubscriberSocket : IZLinkBackendConnectableSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void SetSubscription(string topic);

    global::Zlink.TopicMessage? Subscribe();
}

internal interface IZLinkBackendStreamSocket : IZLinkBackendSocket
{
    void OnRawPacket(Func<string, global::Zlink.Message, int> handler);

    void OnFramedPacket(Action<string, global::Zlink.Message, global::Zlink.Message> handler);

    bool Send(
        global::Zlink.RoutingId routingId,
        global::Zlink.Message payload,
        global::Zlink.SendFlags flags);

    bool Send(
        global::Zlink.RoutingId routingId,
        IReadOnlyList<global::Zlink.Message> parts,
        global::Zlink.SendFlags flags);
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

    IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers(
        ZLinkServiceType serviceType,
        string serviceName);
}

internal interface IZLinkBackendRegistryQueryClient : IZLinkBackendObject, IAsyncDisposable
{
    void Connect(string endpoint);

    IReadOnlyList<ZLinkRegistryTopologyEntry> Snapshot(
        ZLinkRegistryTopologyFilter? filter);
}

internal interface IZLinkBackendSpotNode : IZLinkBackendObject, IAsyncDisposable
{
    global::Zlink.RoutingId RoutingId { get; }

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
    global::Zlink.RoutingId RoutingId { get; }

    void SetRoutingId(global::Zlink.RoutingId routingId);

    void SetSubscription(string topic);

    global::Zlink.TopicMessage? Subscribe(global::Zlink.RecvFlags flags);

    global::Zlink.Received RecvRouted(global::Zlink.RecvFlags flags);

    void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler);

    void DrainChannelReplyFrom(IntPtr dealerSubject);

    ValueTask<IReadOnlyList<global::Zlink.Message>> RequestChannelAsync(
        string channelName,
        global::Zlink.Message message,
        TimeSpan timeout,
        CancellationToken cancellationToken);

    bool SendChannel(
        string channelName,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags);

    bool Publish(
        string serviceName,
        string topic,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags);

    bool SendToSpot(
        global::Zlink.RoutingId targetRid,
        global::Zlink.RoutingId spotRid,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags);
}

internal interface IZLinkBackendSocketMonitor : IZLinkBackendObject, IAsyncDisposable
{
    void OnEvent(Action<ZLinkBackendSocketMonitorEvent> handler);

    ZLinkBackendSocketMonitorEvent Recv();
}

internal interface IZLinkBackendServiceMonitor : IZLinkBackendObject, IAsyncDisposable
{
    void OnEvent(Action<ZLinkBackendDiscoveryMonitorEvent> handler);
}
