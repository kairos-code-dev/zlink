namespace Zlink.Framework.Runtime.Configuration;

internal sealed class ZLinkSocketConfig : IZLinkSocketConfig
{
    private TimeSpan? _sendTimeout;
    private int _weight = 100;

    public long MaxMessageSize { get; set; }

    public int SendHighWaterMark { get; set; }

    public int ReceiveHighWaterMark { get; set; }

    public int SendBufferSize { get; set; }

    public int ReceiveBufferSize { get; set; }

    public TimeSpan? Linger { get; set; }

    public TimeSpan? ReceiveTimeout { get; set; }

    public TimeSpan? SendTimeout
    {
        get => _sendTimeout;
        set
        {
            ValidateSendTimeout(value);
            _sendTimeout = value;
        }
    }

    public TimeSpan? ConnectTimeout { get; set; }

    public TimeSpan? HandshakeInterval { get; set; }

    public bool IPv6 { get; set; }

    public bool TcpNoDelay { get; set; }

    public bool Immediate { get; set; }

    public int Weight
    {
        get => _weight;
        set
        {
            ValidatePeerWeight(value);
            _weight = value;
        }
    }

    internal static void ValidateSendTimeout(TimeSpan? value)
    {
        if (value is { } timeout && timeout < TimeSpan.Zero)
            throw new ZLinkConfigurationException("SendTimeout must be null, zero, or a positive duration.");
    }

    internal static void ValidatePeerWeight(int value)
    {
        if (value is < 0 or > 100) throw new ZLinkConfigurationException("Weight must be between 0 and 100.");
    }
}

internal sealed class ZLinkRouteConfig : IZLinkRouteConfig
{
    public RoutingId RoutingId { get; set; }

    public bool RequireKnownPeer { get; set; }

    public bool AllowPeerHandover { get; set; }

    public bool EnablePeerProbe { get; set; }

    public RoutingId ConnectRoutingId { get; set; }
}

internal sealed class ZLinkOutboundRouteConfig : IZLinkOutboundRouteConfig
{
    public RoutingId RoutingId { get; set; }

    public bool ProbeRouterOnConnect { get; set; }
}

internal sealed class ZLinkSpotPublisherConfig : IZLinkSpotPublisherConfig
{
    private TimeSpan? _sendTimeout;

    public int SendHighWaterMark { private get; set; }

    public TimeSpan? SendTimeout
    {
        private get => _sendTimeout;
        set
        {
            ZLinkSocketConfig.ValidateSendTimeout(value);
            _sendTimeout = value;
        }
    }

    public TimeSpan? Linger { private get; set; }

    public bool NoDrop { private get; set; }
}

internal sealed class ZLinkSpotSubscriberConfig : IZLinkSpotSubscriberConfig
{
    public int ReceiveHighWaterMark { private get; set; }

    public TimeSpan? ReceiveTimeout { private get; set; }

    public TimeSpan? Linger { private get; set; }
}