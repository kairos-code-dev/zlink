using Microsoft.Extensions.Logging;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Configuration;

public sealed class ConnectionAndConfigContracts
{
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
            RequireKnownPeer = true,
            AllowPeerHandover = true,
            EnablePeerProbe = true,
            ConnectRoutingId = RoutingId.From("peer")
        };

        var outbound = new OutboundRouteConfig
        {
            ProbeRouterOnConnect = true
        };

        IZLinkSpotPublisherConfig publisher = new SpotPublisherConfig
        {
            SendHighWaterMark = 32,
            SendTimeout = TimeSpan.FromMilliseconds(20),
            Linger = TimeSpan.Zero
        };

        IZLinkSpotSubscriberConfig subscriber = new SpotSubscriberConfig
        {
            ReceiveHighWaterMark = 64,
            ReceiveTimeout = TimeSpan.FromMilliseconds(30),
            Linger = TimeSpan.Zero
        };

        var entrySpot = new EntrySpotOptions
        {
            RoutingId = RoutingId.From("entry")
        };

        var dispatch = new DispatchOptions();

        Assert.True(socket.Immediate);
        Assert.True(route.RequireKnownPeer);
        Assert.True(outbound.ProbeRouterOnConnect);
        Assert.Equal(32, publisher.SendHighWaterMark);
        Assert.Equal(TimeSpan.FromMilliseconds(20), publisher.SendTimeout);
        Assert.Equal(TimeSpan.Zero, publisher.Linger);
        Assert.Equal(64, subscriber.ReceiveHighWaterMark);
        Assert.Equal(TimeSpan.FromMilliseconds(30), subscriber.ReceiveTimeout);
        Assert.Equal(TimeSpan.Zero, subscriber.Linger);
        Assert.Equal(RoutingId.From("entry"), entrySpot.RoutingId);
        Assert.NotNull(dispatch.Unhandled);
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

        public int Weight { get; set; }

        public bool IPv6 { get; set; }

        public bool TcpNoDelay { get; set; }

        public bool Immediate { get; set; }
    }

    internal sealed class RouteConfig : IZLinkRouteConfig
    {
        public bool RequireKnownPeer { get; set; }

        public bool AllowPeerHandover { get; set; }

        public bool EnablePeerProbe { get; set; }

        public RoutingId ConnectRoutingId { get; set; }
    }

    internal sealed class OutboundRouteConfig : IZLinkOutboundRouteConfig
    {
        public bool ProbeRouterOnConnect { get; set; }
    }

    internal sealed class SpotPublisherConfig : IZLinkSpotPublisherConfig
    {
        public int SendHighWaterMark { get; set; }

        public TimeSpan? SendTimeout { get; set; }

        public TimeSpan? Linger { get; set; }
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
        public IZLinkUnhandledDispatchOptions Unhandled { get; } = new UnhandledDispatchOptions();

        public IZLinkDiagnosticsOptions Diagnostics { get; } = new DiagnosticsOptions();

        public IZLinkDispatchOptions SetMessageFlowObserver<TObserver>()
            where TObserver : class, IZLinkMessageFlowObserver
        {
            return this;
        }

        public IZLinkDispatchOptions SetMessageFlowObserver(IZLinkMessageFlowObserver observer)
        {
            return this;
        }

        public IZLinkDispatchOptions MessageFlow(ZLinkMessageFlowLogMode mode)
        {
            return this;
        }

        public IZLinkDispatchOptions TraceSampleRate(double rate)
        {
            return this;
        }

        public IZLinkDispatchOptions IncludeMessageSizes(bool include)
        {
            return this;
        }

        public IZLinkDispatchOptions TraceLogFile(string path)
        {
            return this;
        }

        public IZLinkDispatchOptions TraceLabel(string id)
        {
            return this;
        }
    }

    private sealed class UnhandledDispatchOptions : IZLinkUnhandledDispatchOptions
    {
        public ZLinkUnhandledDispatchAction Request { get; set; }

        public ZLinkUnhandledDispatchAction Send { get; set; }

        public ZLinkUnhandledDispatchAction Publish { get; set; }
    }

    private sealed class DiagnosticsOptions : IZLinkDiagnosticsOptions
    {
        public ZLinkMessageFlowLogMode MessageFlow { get; set; }

        public double SampleRate { get; set; }

        public bool IncludeMessageSizes { get; set; }

        public bool IncludeNativeDiagnostics { get; set; }

        public string? LogFile { get; set; }

        public string? Label { get; set; }

        public ZLinkMessageFlowLogMode EffectiveMessageFlow => MessageFlow;
    }
}
