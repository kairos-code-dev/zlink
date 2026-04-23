namespace Zlink.Framework;

public interface IZLinkCommonSocketOptions
{
    long MaxMessageSize { get; set; }

    int SendHighWaterMark { get; set; }

    int ReceiveHighWaterMark { get; set; }

    int SendBufferSize { get; set; }

    int ReceiveBufferSize { get; set; }

    TimeSpan? Linger { get; set; }

    TimeSpan? ReceiveTimeout { get; set; }

    TimeSpan? SendTimeout { get; set; }

    TimeSpan? ConnectTimeout { get; set; }

    TimeSpan? HandshakeInterval { get; set; }

    bool IPv6 { get; set; }

    bool TcpNoDelay { get; set; }

    bool Immediate { get; set; }
}

public interface IRoutedPeerOptions
{
    global::Zlink.RoutingId RoutingId { get; set; }

    bool Mandatory { get; set; }

    bool Handover { get; set; }

    bool Probe { get; set; }

    global::Zlink.RoutingId ConnectRoutingId { get; set; }
}

public interface IOutboundPeerOptions
{
    global::Zlink.RoutingId RoutingId { get; set; }

    bool ProbeRouter { get; set; }
}

public interface ISpotNodePublisherOptions
{
    int SendHighWaterMark { set; }

    TimeSpan? SendTimeout { set; }

    TimeSpan? Linger { set; }

    bool NoDrop { set; }
}

public interface ISpotNodeSubscriberOptions
{
    int ReceiveHighWaterMark { set; }

    TimeSpan? ReceiveTimeout { set; }

    TimeSpan? Linger { set; }
}

internal sealed class ZLinkCommonSocketOptions : IZLinkCommonSocketOptions
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

internal sealed class ZLinkRoutedPeerOptions : IRoutedPeerOptions
{
    public global::Zlink.RoutingId RoutingId { get; set; }

    public bool Mandatory { get; set; }

    public bool Handover { get; set; }

    public bool Probe { get; set; }

    public global::Zlink.RoutingId ConnectRoutingId { get; set; }
}

internal sealed class ZLinkOutboundPeerOptions : IOutboundPeerOptions
{
    public global::Zlink.RoutingId RoutingId { get; set; }

    public bool ProbeRouter { get; set; }
}

internal sealed class ZLinkSpotNodePublisherOptions : ISpotNodePublisherOptions
{
    public int SendHighWaterMark { private get; set; }

    public TimeSpan? SendTimeout { private get; set; }

    public TimeSpan? Linger { private get; set; }

    public bool NoDrop { private get; set; }
}

internal sealed class ZLinkSpotNodeSubscriberOptions : ISpotNodeSubscriberOptions
{
    public int ReceiveHighWaterMark { private get; set; }

    public TimeSpan? ReceiveTimeout { private get; set; }

    public TimeSpan? Linger { private get; set; }
}
