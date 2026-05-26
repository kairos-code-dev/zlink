using Microsoft.Extensions.Logging;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Configuration;

public sealed class ConnectionAndConfigContracts
{
    [Fact]
    [ContractExample(
        typeof(IChannelClientConnections),
        typeof(IChannelSubscriberConnections),
        typeof(ISpotRouterConnections),
        typeof(ISpotPubSubConnections),
        typeof(ISpotPublisherConnections),
        typeof(ISpotRouterChannelConnections))]
    public void Connection_contracts_record_the_startup_endpoints_owned_by_each_runtime_role()
    {
        var manual = new ManualConnections();
        IChannelClientConnections channelClient = manual;
        IChannelSubscriberConnections subscriber = manual;
        ISpotRouterConnections spotRouter = manual;
        ISpotPubSubConnections spotPubSub = manual;
        ISpotPublisherConnections spotPublisher = manual;
        ISpotRouterChannelConnections spotRouteIngress = manual;

        channelClient.Connect("tcp://127.0.0.1:5001");
        subscriber.Connect("tcp://127.0.0.1:5002");
        spotRouter.Connect("tcp://127.0.0.1:5003");
        spotPubSub.Connect("tcp://127.0.0.1:5004");
        spotPublisher.Connect("tcp://127.0.0.1:5005");
        spotRouteIngress.Connect("tcp://127.0.0.1:5006");

        channelClient.Disconnect("tcp://127.0.0.1:5001");
        spotRouteIngress.Disconnect("tcp://127.0.0.1:5006");

        Assert.Equal(
            [
                "tcp://127.0.0.1:5002",
                "tcp://127.0.0.1:5003",
                "tcp://127.0.0.1:5004",
                "tcp://127.0.0.1:5005"
            ],
            spotRouteIngress.ListConnections());
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSocketConfig),
        typeof(IZLinkRouteConfig),
        typeof(IZLinkOutboundRouteConfig),
        typeof(IZLinkSpotPublisherConfig),
        typeof(IZLinkSpotSubscriberConfig),
        typeof(IZLinkEntrySpotOptions),
        typeof(IZLinkDispatchOptions),
        typeof(IZLinkUnhandledDispatchOptions),
        typeof(IZLinkDiagnosticsOptions))]
    public void Configuration_contracts_keep_socket_routing_spot_and_dispatch_options_typed()
    {
        var socket = new SocketConfig
        {
            MaxMessageSize = 1024,
            SendHighWaterMark = 10,
            ReceiveHighWaterMark = 20,
            SendBufferSize = 4096,
            ReceiveBufferSize = 8192,
            Linger = TimeSpan.Zero,
            ReceiveTimeout = TimeSpan.FromSeconds(1),
            SendTimeout = TimeSpan.FromSeconds(2),
            ConnectTimeout = TimeSpan.FromSeconds(3),
            HandshakeInterval = TimeSpan.FromSeconds(4),
            IPv6 = true,
            TcpNoDelay = true,
            Immediate = true
        };

        var route = new RouteConfig
        {
            RoutingId = RoutingId.Of("router"),
            RequireKnownPeer = true,
            AllowPeerHandover = true,
            EnablePeerProbe = true,
            ConnectRoutingId = RoutingId.Of("peer")
        };

        var outbound = new OutboundRouteConfig
        {
            RoutingId = RoutingId.Of("client"),
            ProbeRouterOnConnect = true
        };

        var publisher = new SpotPublisherConfig
        {
            SendHighWaterMark = 32,
            SendTimeout = TimeSpan.FromMilliseconds(20),
            Linger = TimeSpan.Zero,
            NoDrop = true
        };

        var subscriber = new SpotSubscriberConfig
        {
            ReceiveHighWaterMark = 64,
            ReceiveTimeout = TimeSpan.FromMilliseconds(30),
            Linger = TimeSpan.Zero
        };

        var entrySpot = new EntrySpotOptions
        {
            RoutingId = RoutingId.Of("entry")
        };

        var dispatch = new DispatchOptions
        {
            SpotDispatchMode = ZLinkDispatchMode.Compiled,
            StreamDispatchMode = ZLinkDispatchMode.Dynamic
        };

        Assert.True(socket.Immediate);
        Assert.Equal(RoutingId.Of("router"), route.RoutingId);
        Assert.Equal(RoutingId.Of("client"), outbound.RoutingId);
        Assert.True(publisher.NoDrop);
        Assert.Equal(64, subscriber.ReceiveHighWaterMark);
        Assert.Equal(RoutingId.Of("entry"), entrySpot.RoutingId);
        Assert.Equal(ZLinkDispatchMode.Compiled, dispatch.SpotDispatchMode);
    }

    internal sealed class ManualConnections :
        IChannelClientConnections,
        IChannelSubscriberConnections,
        ISpotRouterConnections,
        ISpotPubSubConnections,
        ISpotPublisherConnections,
        IRouteChannelConnections,
        ISpotRouterChannelConnections
    {
        private readonly List<string> _endpoints = [];

        public void Connect(string endpoint) => _endpoints.Add(endpoint);

        public void Disconnect(string endpoint) => _endpoints.Remove(endpoint);

        public IReadOnlyList<string> ListConnections() => _endpoints.ToArray();

    }

    internal sealed class SocketConfig : IZLinkSocketConfig
    {
        public long MaxMessageSize { get; set; }

        public int SendHighWaterMark { get; set; }

        public int ReceiveHighWaterMark { get; set; }

        public int SendBufferSize { get; set; }

        public int ReceiveBufferSize { get; set; }

        public TimeSpan? Linger { get; set; }

        public TimeSpan? ReceiveTimeout { get; set; }

        public TimeSpan? SendTimeout { get; set; }

        public TimeSpan? ConnectTimeout { get; set; }

        public TimeSpan? HandshakeInterval { get; set; }

        public bool IPv6 { get; set; }

        public bool TcpNoDelay { get; set; }

        public bool Immediate { get; set; }
    }

    internal sealed class RouteConfig : IZLinkRouteConfig
    {
        public RoutingId RoutingId { get; set; }

        public bool RequireKnownPeer { get; set; }

        public bool AllowPeerHandover { get; set; }

        public bool EnablePeerProbe { get; set; }

        public RoutingId ConnectRoutingId { get; set; }
    }

    internal sealed class OutboundRouteConfig : IZLinkOutboundRouteConfig
    {
        public RoutingId RoutingId { get; set; }

        public bool ProbeRouterOnConnect { get; set; }
    }

    internal sealed class SpotPublisherConfig : IZLinkSpotPublisherConfig
    {
        public int SendHighWaterMark { get; set; }

        public TimeSpan? SendTimeout { get; set; }

        public TimeSpan? Linger { get; set; }

        public bool NoDrop { get; set; }
    }

    internal sealed class SpotSubscriberConfig : IZLinkSpotSubscriberConfig
    {
        public int ReceiveHighWaterMark { get; set; }

        public TimeSpan? ReceiveTimeout { get; set; }

        public TimeSpan? Linger { get; set; }
    }

    internal sealed class EntrySpotOptions : IZLinkEntrySpotOptions
    {
        public RoutingId RoutingId { get; set; }
    }

    internal sealed class DispatchOptions : IZLinkDispatchOptions
    {
        public ZLinkDispatchMode SpotDispatchMode { get; set; }

        public ZLinkDispatchMode StreamDispatchMode { get; set; }

        public IZLinkUnhandledDispatchOptions Unhandled { get; } = new UnhandledDispatchOptions();

        public IZLinkDiagnosticsOptions Diagnostics { get; } = new DiagnosticsOptions();
    }

    private sealed class UnhandledDispatchOptions : IZLinkUnhandledDispatchOptions
    {
        public ZLinkUnhandledDispatchAction Request { get; set; }

        public ZLinkUnhandledDispatchAction Send { get; set; }

        public ZLinkUnhandledDispatchAction Publish { get; set; }

        public LogLevel SendLogLevel { get; set; }

        public LogLevel PublishLogLevel { get; set; }
    }

    private sealed class DiagnosticsOptions : IZLinkDiagnosticsOptions
    {
        public ZLinkMessageFlowLogMode MessageFlow { get; set; }

        public double SampleRate { get; set; }

        public bool IncludeMessageSizes { get; set; }

        public bool IncludeNativeDiagnostics { get; set; }
    }
}
