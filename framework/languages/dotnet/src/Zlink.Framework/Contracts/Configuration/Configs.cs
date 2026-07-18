namespace Zlink.Framework.Contracts.Configuration;

public interface IZLinkSocketConfig
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

    /// <summary>
    ///     Advertised peer weight (0..100) used by connected peers for outbound
    ///     load-balancing selection. 0 = drain (peers stop selecting this node for
    ///     new requests; in-flight and reply paths are unaffected), 100 = normal
    ///     (default). Settable at build time and at runtime via
    ///     <c>IZLinkRouteMeshRuntimeOptions.Channel(meshName, channelName).Weight</c>;
    ///     runtime changes apply immediately to new outbound peer selections.
    /// </summary>
    int Weight { get; set; }
}

public interface IZLinkRouteConfig
{
    bool RequireKnownPeer { get; set; }

    bool AllowPeerHandover { get; set; }

    bool EnablePeerProbe { get; set; }

    RoutingId ConnectRoutingId { get; set; }
}

public interface IZLinkOutboundRouteConfig
{
    bool ProbeRouterOnConnect { get; set; }
}

public interface IZLinkSpotPublisherConfig
{
    int SendHighWaterMark { get; set; }

    TimeSpan? SendTimeout { get; set; }

    TimeSpan? Linger { get; set; }

    bool NoDrop { get; set; }
}

public interface IZLinkSpotSubscriberConfig
{
    int ReceiveHighWaterMark { get; set; }

    TimeSpan? ReceiveTimeout { get; set; }

    TimeSpan? Linger { get; set; }
}

public interface IZLinkEntrySpotOptions
{
    RoutingId RoutingId { get; set; }
}
