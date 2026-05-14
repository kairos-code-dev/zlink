namespace Zlink.Framework.Configuration;

internal sealed class ZLinkCommonSocketOptions : IZLinkCommonSocketOptions
{
    private TimeSpan? _sendTimeout = TimeSpan.FromMilliseconds(200);

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

    internal static void ValidateSendTimeout(TimeSpan? value)
    {
        if (value is { } timeout && timeout < TimeSpan.Zero)
        {
            throw new ZLinkConfigurationException("SendTimeout must be null, zero, or a positive duration.");
        }
    }
}

internal sealed class ZLinkRoutePeerOptions : IZLinkRoutePolicyOptions
{
    public RoutingId RoutingId { get; set; }

    public bool RequireKnownPeer { get; set; }

    public bool AllowPeerHandover { get; set; }

    public bool EnablePeerProbe { get; set; }

    public RoutingId ConnectRoutingId { get; set; }
}

internal sealed class ZLinkOutboundPeerOptions : IZLinkOutboundRoutePolicyOptions
{
    public RoutingId RoutingId { get; set; }

    public bool ProbeRouterOnConnect { get; set; }
}

internal sealed class ZLinkSpotNodePublisherOptions : ISpotNodePublisherOptions
{
    private TimeSpan? _sendTimeout = TimeSpan.FromMilliseconds(200);

    public int SendHighWaterMark { private get; set; }

    public TimeSpan? SendTimeout
    {
        private get => _sendTimeout;
        set
        {
            ZLinkCommonSocketOptions.ValidateSendTimeout(value);
            _sendTimeout = value;
        }
    }

    public TimeSpan? Linger { private get; set; }

    public bool NoDrop { private get; set; }
}

internal sealed class ZLinkSpotNodeSubscriberOptions : ISpotNodeSubscriberOptions
{
    public int ReceiveHighWaterMark { private get; set; }

    public TimeSpan? ReceiveTimeout { private get; set; }

    public TimeSpan? Linger { private get; set; }
}
