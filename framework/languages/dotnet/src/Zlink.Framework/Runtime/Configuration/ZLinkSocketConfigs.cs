namespace Zlink.Framework.Runtime.Configuration;

// Also surfaces the MeshNode ROUTER build-time option subset via
// IZLinkMeshNodeSocketConfig (spec 05-route-mesh §5). Every member the mesh-node
// interface exposes (MaxMessageSize/HWM/timeouts) already lives here.
internal sealed class ZLinkSocketConfig : IZLinkSocketConfig, IZLinkMeshNodeSocketConfig
{
    internal const int DefaultPeerWeight = 100;

    private TimeSpan? _sendTimeout;
    private int _weight = DefaultPeerWeight;

    public long MaxMessageSize { get; set; }

    public int SendHighWaterMark { get; set; }

    public int ReceiveHighWaterMark { get; set; }

    public ulong MailboxMessageBudget { get; set; }

    public ulong MailboxByteBudget { get; set; }

    public int SendBufferSize { get; set; }

    public int ReceiveBufferSize { get; set; }

    public TimeSpan? Linger { get; set; } = TimeSpan.Zero;

    public TimeSpan? ReceiveTimeout { get; set; }

    public TimeSpan? SendTimeout
    {
        get => _sendTimeout;
        set
        {
            _sendTimeout = NormalizeSendTimeout(value);
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

    internal static TimeSpan? NormalizeSendTimeout(TimeSpan? value)
    {
        if (value is not { } timeout) return null;
        if (timeout <= TimeSpan.Zero)
            throw new ZLinkConfigurationException("SendTimeout must be greater than zero.");

        var wholeMilliseconds = timeout.Ticks / TimeSpan.TicksPerMillisecond;
        if (timeout.Ticks % TimeSpan.TicksPerMillisecond != 0) wholeMilliseconds++;
        if (wholeMilliseconds > int.MaxValue)
            throw new ZLinkConfigurationException(
                $"SendTimeout must not exceed {int.MaxValue} milliseconds.");

        return TimeSpan.FromMilliseconds(wholeMilliseconds);
    }

    internal static void ValidatePeerWeight(int value)
    {
        if (value is < 0 or > DefaultPeerWeight)
            throw new ZLinkConfigurationException($"Weight must be between 0 and {DefaultPeerWeight}.");
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

    public int SendHighWaterMark { get; set; }

    public TimeSpan? SendTimeout
    {
        get => _sendTimeout;
        set
        {
            _sendTimeout = ZLinkSocketConfig.NormalizeSendTimeout(value);
        }
    }

    public TimeSpan? Linger { get; set; }
}

internal sealed class ZLinkSpotSubscriberConfig : IZLinkSpotSubscriberConfig
{
    public int ReceiveHighWaterMark { get; set; }

    public TimeSpan? ReceiveTimeout { get; set; }

    public TimeSpan? Linger { get; set; }
}
