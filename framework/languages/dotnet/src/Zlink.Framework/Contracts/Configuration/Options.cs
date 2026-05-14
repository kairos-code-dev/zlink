namespace Zlink.Framework.Contracts.Configuration;

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

public interface IZLinkRoutePolicyOptions
{
    RoutingId RoutingId { get; set; }

    bool RequireKnownPeer { get; set; }

    bool AllowPeerHandover { get; set; }

    bool EnablePeerProbe { get; set; }

    RoutingId ConnectRoutingId { get; set; }
}

public interface IZLinkOutboundRoutePolicyOptions
{
    RoutingId RoutingId { get; set; }

    bool ProbeRouterOnConnect { get; set; }
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
