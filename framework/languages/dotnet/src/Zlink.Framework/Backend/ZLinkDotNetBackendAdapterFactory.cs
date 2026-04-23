using Zlink.Framework;

namespace Zlink.Framework.Backend;

internal sealed class ZLinkDotNetBackendAdapterFactory : IZLinkBackendAdapterFactory
{
    private static readonly IZLinkChannelBackendAdapter ChannelAdapter = new ZLinkDotNetChannelBackendAdapter();
    private static readonly IZLinkSpotBackendAdapter SpotAdapter = new ZLinkDotNetSpotBackendAdapter();
    private static readonly IZLinkStreamBackendAdapter StreamAdapter = new ZLinkDotNetStreamBackendAdapter();
    private static readonly IZLinkRegistryBackendAdapter RegistryAdapter = new ZLinkDotNetRegistryBackendAdapter();
    private static readonly IZLinkMonitoringBackendAdapter MonitoringAdapter = new ZLinkDotNetMonitoringBackendAdapter();

    public IZLinkChannelBackendAdapter CreateChannelAdapter() => ChannelAdapter;

    public IZLinkSpotBackendAdapter CreateSpotAdapter() => SpotAdapter;

    public IZLinkStreamBackendAdapter CreateStreamAdapter() => StreamAdapter;

    public IZLinkRegistryBackendAdapter CreateRegistryAdapter() => RegistryAdapter;

    public IZLinkMonitoringBackendAdapter CreateMonitoringAdapter() => MonitoringAdapter;
}

internal sealed class ZLinkDotNetChannelBackendAdapter : IZLinkChannelBackendAdapter
{
    public IZLinkBackendContext CreateContext()
    {
        return new ZLinkBackendContextWrapper(new global::Zlink.Context());
    }

    public IZLinkBackendDiscovery CreateDiscovery(
        IZLinkBackendContext context,
        ZLinkBackendServiceType serviceType,
        string serviceName)
    {
        var nativeContext = context.RequireNative<global::Zlink.Context>();
        var nativeServiceType = serviceType switch
        {
            ZLinkBackendServiceType.Socket => global::Zlink.ServiceType.Socket,
            ZLinkBackendServiceType.Spot => global::Zlink.ServiceType.Spot,
            _ => throw new ArgumentOutOfRangeException(nameof(serviceType)),
        };

        return new ZLinkBackendDiscoveryWrapper(
            new global::Zlink.Discovery(nativeContext, nativeServiceType, serviceName));
    }

    public IZLinkBackendDealerSocket CreateDealerSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendDealerSocketWrapper(
            new global::Zlink.DealerSocket(context.RequireNative<global::Zlink.Context>()));
    }

    public IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendRouterSocketWrapper(
            new global::Zlink.RouterSocket(context.RequireNative<global::Zlink.Context>()));
    }

    public IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendPublisherSocketWrapper(
            new global::Zlink.PubSocket(context.RequireNative<global::Zlink.Context>()));
    }

    public IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendSubscriberSocketWrapper(
            new global::Zlink.SubSocket(context.RequireNative<global::Zlink.Context>()));
    }
}

internal sealed class ZLinkDotNetSpotBackendAdapter : IZLinkSpotBackendAdapter
{
    public IZLinkBackendSpotNode CreateSpotNode(IZLinkBackendContext context)
    {
        return new ZLinkBackendSpotNodeWrapper(
            new global::Zlink.SpotNode(context.RequireNative<global::Zlink.Context>()));
    }
}

internal sealed class ZLinkDotNetStreamBackendAdapter : IZLinkStreamBackendAdapter
{
    public IZLinkBackendStreamSocket CreateStreamSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendStreamSocketWrapper(
            new global::Zlink.StreamSocket(context.RequireNative<global::Zlink.Context>()));
    }
}

internal sealed class ZLinkDotNetRegistryBackendAdapter : IZLinkRegistryBackendAdapter
{
    public IZLinkBackendRegistry CreateRegistry(IZLinkBackendContext context)
    {
        return new ZLinkBackendRegistryWrapper(
            new global::Zlink.Registry(context.RequireNative<global::Zlink.Context>()));
    }

    public IZLinkBackendRegistryQueryClient CreateRegistryQueryClient(IZLinkBackendContext context)
    {
        return new ZLinkBackendRegistryQueryClientWrapper(
            new global::Zlink.RegistryQueryClient(context.RequireNative<global::Zlink.Context>()));
    }
}

internal sealed class ZLinkDotNetMonitoringBackendAdapter : IZLinkMonitoringBackendAdapter
{
    public IZLinkBackendSocketMonitor OpenSocketMonitor(IZLinkBackendSocket socket)
    {
        var nativeMonitor = socket.OpenNativeMonitor();
        return new ZLinkBackendSocketMonitorWrapper(nativeMonitor);
    }

    public IZLinkBackendServiceMonitor OpenDiscoveryMonitor(IZLinkBackendDiscovery discovery)
    {
        var nativeMonitor = discovery.RequireNative<global::Zlink.Discovery>().MonitorOpen();
        return new ZLinkBackendServiceMonitorWrapper(nativeMonitor);
    }
}

internal static class ZLinkBackendNativeAccess
{
    public static T RequireNative<T>(this IZLinkBackendObject backendObject)
        where T : class
    {
        return backendObject.NativeInstance as T
            ?? throw new InvalidOperationException(
                $"Expected native instance '{typeof(T).FullName}'.");
    }

    public static global::Zlink.SocketMonitor OpenNativeMonitor(this IZLinkBackendSocket socket)
    {
        return socket.NativeInstance switch
        {
            global::Zlink.DealerSocket native => native.MonitorOpen(),
            global::Zlink.RouterSocket native => native.MonitorOpen(),
            global::Zlink.PubSocket native => native.MonitorOpen(),
            global::Zlink.SubSocket native => native.MonitorOpen(),
            global::Zlink.StreamSocket native => native.MonitorOpen(),
            _ => throw new InvalidOperationException("Expected a native socket instance."),
        };
    }
}

internal static class ZLinkDotNetBackendMappings
{
    public static ZLinkRegistryStatus ToFramework(this global::Zlink.RegistryStatus status)
    {
        return new ZLinkRegistryStatus(
            status.RegistryId,
            status.BindEndpoint,
            (ZLinkRegistryState)status.State,
            status.TopologyEntryCount,
            status.PeerRegistryCount,
            status.ConnectedPeerRegistryCount,
            status.ListSeq,
            status.LastError,
            status.LastChangedMs);
    }

    public static ZLinkRegistryServiceSummaryEntry ToFramework(this global::Zlink.RegistryServiceSummaryEntry entry)
    {
        return new ZLinkRegistryServiceSummaryEntry(
            (ZLinkServiceKind)entry.ServiceKind,
            (ZLinkServiceRole)entry.ServiceRole,
            entry.ServiceName,
            entry.TotalCount,
            entry.ConnectingCount,
            entry.ReadyCount,
            entry.ErrorCount,
            entry.StoppedCount,
            entry.LastReportedMs);
    }

    public static ZLinkRegistryTopologyEntry ToFramework(this global::Zlink.RegistryTopologyEntry entry)
    {
        return new ZLinkRegistryTopologyEntry(
            entry.RoutingId,
            (ZLinkServiceKind)entry.ServiceKind,
            (ZLinkServiceRole)entry.ServiceRole,
            entry.ServiceName,
            entry.Endpoint,
            (ZLinkTopologySource)entry.Source,
            (ZLinkTopologyState)entry.State,
            entry.DesiredCount,
            entry.ReadyCount,
            entry.ErrorCode,
            entry.LastReportedMs);
    }

    public static ZLinkMemberPeerEntry ToFramework(this global::Zlink.MemberPeerEntry entry)
    {
        return new ZLinkMemberPeerEntry(
            (ZLinkServiceType)entry.ServiceType,
            (ZLinkServiceRole)entry.ServiceRole,
            entry.ServiceName,
            entry.Endpoint,
            entry.RoutingId,
            entry.Value,
            (ZLinkAdmissionState)entry.AdmissionState);
    }

    public static ZLinkSpotNodeStatus ToFramework(this global::Zlink.SpotNodeStatus status)
    {
        return new ZLinkSpotNodeStatus(
            status.ServiceName,
            status.LocalEndpoint,
            status.NodeRoutingId,
            (ZLinkSpotNodeState)status.State,
            status.ConfiguredPeerCount,
            status.ActivePeerCount,
            status.ConnectedPeerCount,
            status.SubjectCount,
            status.ReadySubjectCount,
            status.LastError,
            status.LastChangedMs);
    }

    public static ZLinkSpotNodePeerEntry ToFramework(this global::Zlink.SpotNodePeerEntry entry)
    {
        return new ZLinkSpotNodePeerEntry(
            entry.ServiceName,
            entry.LocalEndpoint,
            entry.PeerEndpoint,
            (ZLinkSpotPeerSource)entry.Source,
            (ZLinkSpotPeerState)entry.State,
            (ZLinkAdmissionState)entry.AdmissionState,
            entry.ConnectedSinceMs,
            entry.LastChangedMs);
    }

    public static ZLinkSpotNodeSubjectEntry ToFramework(this global::Zlink.SpotNodeSubjectEntry entry)
    {
        return new ZLinkSpotNodeSubjectEntry(
            (ZLinkSpotRole)entry.Role,
            entry.Subject,
            (ZLinkSubjectKind)entry.SubjectKind,
            entry.ReadyPeerCount,
            entry.ActivePeerCount,
            entry.LastChangedMs);
    }

    public static global::Zlink.RegistryServiceSummaryFilter? ToNative(this ZLinkRegistryServiceSummaryFilter? filter)
    {
        if (filter is null)
        {
            return null;
        }

        return new global::Zlink.RegistryServiceSummaryFilter(
            filter.ServiceKind is null ? null : (global::Zlink.ServiceKind?)filter.ServiceKind,
            filter.ServiceRole is null ? null : (global::Zlink.ServiceRole?)filter.ServiceRole,
            filter.ServiceName);
    }

    public static global::Zlink.RegistryTopologyFilter? ToNative(this ZLinkRegistryTopologyFilter? filter)
    {
        if (filter is null)
        {
            return null;
        }

        return new global::Zlink.RegistryTopologyFilter(
            filter.ServiceKind is null ? null : (global::Zlink.ServiceKind?)filter.ServiceKind,
            filter.ServiceRole is null ? null : (global::Zlink.ServiceRole?)filter.ServiceRole,
            filter.ServiceName,
            filter.RoutingId,
            filter.State is null ? null : (global::Zlink.TopologyState?)filter.State,
            filter.Source is null ? null : (global::Zlink.TopologySource?)filter.Source);
    }

    public static ZLinkBackendSocketMonitorEvent ToFramework(this global::Zlink.MonitorEvent monitorEvent)
    {
        return new ZLinkBackendSocketMonitorEvent(
            (ZLinkSocketNativeEventType)monitorEvent.Event,
            monitorEvent.RoutingId,
            monitorEvent.LocalAddr,
            monitorEvent.RemoteAddr,
            monitorEvent.Value);
    }

    public static ZLinkBackendDiscoveryMonitorEvent ToFramework(this global::Zlink.ServiceEvent serviceEvent)
    {
        return new ZLinkBackendDiscoveryMonitorEvent(
            (ZLinkDiscoveryNativeEventType)serviceEvent.EventType,
            serviceEvent.ServiceName,
            serviceEvent.Endpoint,
            serviceEvent.RoutingId,
            serviceEvent.Subject,
            (ZLinkSubjectKind)serviceEvent.SubjectKind,
            serviceEvent.Status,
            serviceEvent.ErrorCode,
            serviceEvent.Value,
            serviceEvent.DetailFlags);
    }

    public static ZLinkBackendSpotDispatchInfo ToFramework(this global::Zlink.SpotDispatchInfo info)
    {
        return new ZLinkBackendSpotDispatchInfo(
            info.Event switch
            {
                global::Zlink.SpotDispatchEvent.RoutedReadable => ZLinkBackendSpotDispatchEvent.RoutedReadable,
                global::Zlink.SpotDispatchEvent.ChannelReplyReadable => ZLinkBackendSpotDispatchEvent.ChannelReplyReadable,
                _ => ZLinkBackendSpotDispatchEvent.Internal,
            },
            info.Subject);
    }
}

internal sealed class ZLinkBackendContextWrapper(global::Zlink.Context nativeContext) : IZLinkBackendContext
{
    public object NativeInstance => nativeContext;

    public ValueTask DisposeAsync() => nativeContext.DisposeAsync();
}

internal sealed class ZLinkBackendDiscoveryWrapper(global::Zlink.Discovery nativeDiscovery) : IZLinkBackendDiscovery
{
    public object NativeInstance => nativeDiscovery;

    public void ConnectRegistry(string endpoint)
    {
        nativeDiscovery.ConnectRegistry(endpoint);
    }

    public ValueTask DisposeAsync() => nativeDiscovery.DisposeAsync();
}

internal sealed class ZLinkBackendDealerSocketWrapper(global::Zlink.DealerSocket nativeSocket) : IZLinkBackendDealerSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void Connect(string endpoint)
    {
        nativeSocket.Connect(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        nativeSocket.Disconnect(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public bool Send(global::Zlink.Message message, global::Zlink.SendFlags flags)
    {
        return nativeSocket.Send(message, flags);
    }

    public async ValueTask<IReadOnlyList<global::Zlink.Message>> RequestAsync(
        global::Zlink.Message message,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await nativeSocket.RequestAsync(message, timeout, cancellationToken).ConfigureAwait(false);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendRouterSocketWrapper(global::Zlink.RouterSocket nativeSocket) : IZLinkBackendRouterSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public global::Zlink.Received? Recv()
    {
        return nativeSocket.Recv();
    }

    public void Reply(
        global::Zlink.RoutingId routingId,
        ulong requestSeq,
        global::Zlink.Message message)
    {
        nativeSocket.Reply(routingId, requestSeq, message);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendPublisherSocketWrapper(global::Zlink.PubSocket nativeSocket) : IZLinkBackendPublisherSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public bool Publish(
        string topic,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return nativeSocket.Publish(topic, message, flags);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendSubscriberSocketWrapper(global::Zlink.SubSocket nativeSocket) : IZLinkBackendSubscriberSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void Connect(string endpoint)
    {
        nativeSocket.Connect(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        nativeSocket.Disconnect(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public void SetSubscription(string topic)
    {
        nativeSocket.SetSubscription(topic);
    }

    public global::Zlink.TopicMessage? Subscribe()
    {
        return nativeSocket.Subscribe();
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendStreamSocketWrapper(global::Zlink.StreamSocket nativeSocket) : IZLinkBackendStreamSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void OnRawPacket(Func<string, global::Zlink.Message, int> handler)
    {
        nativeSocket.OnPacket(handler.Invoke);
    }

    public void OnFramedPacket(Action<string, global::Zlink.Message, global::Zlink.Message> handler)
    {
        nativeSocket.OnFramedPacket((routingIdText, header, body) =>
        {
            handler(routingIdText, header, body);
        });
    }

    public bool Send(
        global::Zlink.RoutingId routingId,
        global::Zlink.Message payload,
        global::Zlink.SendFlags flags)
    {
        return nativeSocket.Send(routingId, payload, flags);
    }

    public bool Send(
        global::Zlink.RoutingId routingId,
        IReadOnlyList<global::Zlink.Message> parts,
        global::Zlink.SendFlags flags)
    {
        return nativeSocket.Send(routingId, parts, flags);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendRegistryWrapper(global::Zlink.Registry nativeRegistry) : IZLinkBackendRegistry
{
    public object NativeInstance => nativeRegistry;

    public void SetId(uint registryId)
    {
        nativeRegistry.SetId(registryId);
    }

    public void SetHeartbeat(uint intervalMs, uint timeoutMs)
    {
        nativeRegistry.SetHeartbeat(intervalMs, timeoutMs);
    }

    public void SetBroadcastInterval(uint intervalMs)
    {
        nativeRegistry.SetBroadcastInterval(intervalMs);
    }

    public void AddPeer(string endpoint)
    {
        nativeRegistry.AddPeer(endpoint);
    }

    public void Bind(string pubEndpoint, string routerEndpoint)
    {
        nativeRegistry.Bind(pubEndpoint, routerEndpoint);
    }

    public ZLinkRegistryStatus StatusSnapshot()
    {
        return nativeRegistry.StatusSnapshot().ToFramework();
    }

    public IReadOnlyList<ZLinkRegistryServiceSummaryEntry> ServiceSummarySnapshot(
        ZLinkRegistryServiceSummaryFilter? filter)
    {
        return nativeRegistry.ServiceSummarySnapshot(filter.ToNative())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkRegistryTopologyEntry> TopologySnapshot()
    {
        return nativeRegistry.TopologySnapshot()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkRegistryTopologyEntry> TopologyQuery(
        ZLinkRegistryTopologyFilter? filter)
    {
        return nativeRegistry.TopologyQuery(filter.ToNative())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers(
        ZLinkServiceType serviceType,
        string serviceName)
    {
        return nativeRegistry.MemberPeers((global::Zlink.ServiceType)serviceType, serviceName)
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public ValueTask DisposeAsync() => nativeRegistry.DisposeAsync();
}

internal sealed class ZLinkBackendRegistryQueryClientWrapper(global::Zlink.RegistryQueryClient nativeClient)
    : IZLinkBackendRegistryQueryClient
{
    public object NativeInstance => nativeClient;

    public void Connect(string endpoint)
    {
        nativeClient.Connect(endpoint);
    }

    public IReadOnlyList<ZLinkRegistryTopologyEntry> Snapshot(
        ZLinkRegistryTopologyFilter? filter)
    {
        return nativeClient.Snapshot(filter.ToNative())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public ValueTask DisposeAsync() => nativeClient.DisposeAsync();
}

internal sealed class ZLinkBackendSpotNodeWrapper(global::Zlink.SpotNode nativeSpotNode) : IZLinkBackendSpotNode
{
    public object NativeInstance => nativeSpotNode;

    public global::Zlink.RoutingId RoutingId => nativeSpotNode.RoutingId;

    public void Bind(string endpoint)
    {
        nativeSpotNode.Bind(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSpotNode.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public void ConnectPeer(string endpoint)
    {
        nativeSpotNode.ConnectPeer(endpoint);
    }

    public void DisconnectPeer(string endpoint)
    {
        nativeSpotNode.DisconnectPeer(endpoint);
    }

    public IZLinkBackendSpot CreateSpot()
    {
        return new ZLinkBackendSpotWrapper(nativeSpotNode.CreateSpot());
    }

    public ZLinkSpotNodeStatus StatusSnapshot()
    {
        return nativeSpotNode.StatusSnapshot().ToFramework();
    }

    public IReadOnlyList<ZLinkSpotNodePeerEntry> PeersSnapshot()
    {
        return nativeSpotNode.PeersSnapshot()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkSpotNodeSubjectEntry> SubjectsSnapshot()
    {
        return nativeSpotNode.SubjectsSnapshot()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public void AttachChannelDealer(IZLinkBackendDiscovery discovery, IZLinkBackendDealerSocket dealer)
    {
        nativeSpotNode.AttachChannelDealer(
            discovery.RequireNative<global::Zlink.Discovery>(),
            dealer.RequireNative<global::Zlink.DealerSocket>());
    }

    public void AttachChannelDealerManual(string channelName, IZLinkBackendDealerSocket dealer)
    {
        nativeSpotNode.AttachChannelDealerManual(
            channelName,
            dealer.RequireNative<global::Zlink.DealerSocket>());
    }

    public ValueTask DisposeAsync() => nativeSpotNode.DisposeAsync();
}

internal sealed class ZLinkBackendSpotWrapper(global::Zlink.Spot nativeSpot) : IZLinkBackendSpot
{
    public object NativeInstance => nativeSpot;

    public global::Zlink.RoutingId RoutingId => nativeSpot.RoutingId;

    public void SetRoutingId(global::Zlink.RoutingId routingId)
    {
        nativeSpot.SetRoutingId(routingId);
    }

    public void SetSubscription(string topic)
    {
        nativeSpot.SetSubscription(topic);
    }

    public global::Zlink.TopicMessage? Subscribe(global::Zlink.RecvFlags flags)
    {
        return nativeSpot.Subscribe(flags);
    }

    public global::Zlink.Received RecvRouted(global::Zlink.RecvFlags flags)
    {
        return nativeSpot.RecvRouted(flags);
    }

    public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
    {
        nativeSpot.OnDispatchEvent((_, info) => handler(info.ToFramework()));
    }

    public void DrainChannelReplyFrom(IntPtr dealerSubject)
    {
        nativeSpot.DrainChannelReplyFrom(dealerSubject);
    }

    public async ValueTask<IReadOnlyList<global::Zlink.Message>> RequestChannelAsync(
        string channelName,
        global::Zlink.Message message,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await nativeSpot.RequestChannelAsync(
            channelName,
            message,
            timeout,
            cancellationToken).ConfigureAwait(false);
    }

    public bool SendChannel(
        string channelName,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return nativeSpot.SendChannel(channelName, message, flags);
    }

    public bool Publish(
        string serviceName,
        string topic,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return nativeSpot.Publish(serviceName, topic, message, flags);
    }

    public bool SendToSpot(
        global::Zlink.RoutingId targetRid,
        global::Zlink.RoutingId spotRid,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, spotRid, message, flags);
    }

    public ValueTask DisposeAsync() => nativeSpot.DisposeAsync();
}

internal sealed class ZLinkBackendSocketMonitorWrapper(global::Zlink.SocketMonitor nativeMonitor) : IZLinkBackendSocketMonitor
{
    public object NativeInstance => nativeMonitor;

    public void OnEvent(Action<ZLinkBackendSocketMonitorEvent> handler)
    {
        nativeMonitor.OnEvent(monitorEvent => handler(monitorEvent.ToFramework()));
    }

    public ZLinkBackendSocketMonitorEvent Recv()
    {
        return nativeMonitor.Recv().ToFramework();
    }

    public ValueTask DisposeAsync() => nativeMonitor.DisposeAsync();
}

internal sealed class ZLinkBackendServiceMonitorWrapper(global::Zlink.ServiceMonitor nativeMonitor) : IZLinkBackendServiceMonitor
{
    public object NativeInstance => nativeMonitor;

    public void OnEvent(Action<ZLinkBackendDiscoveryMonitorEvent> handler)
    {
        nativeMonitor.OnEvent(serviceEvent => handler(serviceEvent.ToFramework()));
    }

    public ValueTask DisposeAsync() => nativeMonitor.DisposeAsync();
}
