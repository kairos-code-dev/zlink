// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Configures a SPOT node's routing identity, bind endpoints, and runtime options.
/// </summary>
public interface ISpotNodeConfiguration
{
    /// <summary>
    ///     Gets or sets the router high water mark profile.
    /// </summary>
    AutoHwmProfile RouterHwmProfile { get; set; }

    /// <summary>
    ///     Gets or sets the router high water mark.
    /// </summary>
    int RouterHighWaterMark { get; set; }

    /// <summary>
    ///     Gets or sets the pub sub high water mark profile.
    /// </summary>
    AutoHwmProfile PubSubHwmProfile { get; set; }

    /// <summary>
    ///     Gets or sets the pub sub high water mark.
    /// </summary>
    int PubSubHighWaterMark { get; set; }

    /// <summary>
    ///     Sets whether publisher sends should avoid dropping messages.
    /// </summary>
    bool PublisherNoDrop { set; }

    /// <summary>
    ///     Sets the publisher send timeout.
    /// </summary>
    TimeSpan? PublisherSendTimeout { set; }

    /// <summary>Sets how long publisher close waits for queued messages.</summary>
    TimeSpan? PublisherLinger { set; }

    /// <summary>Sets how long subscriber receive waits for a message.</summary>
    TimeSpan? SubscriberReceiveTimeout { set; }

    /// <summary>Sets how long subscriber close waits for queued messages.</summary>
    TimeSpan? SubscriberLinger { set; }

    /// <summary>
    ///     Registers a callback invoked when the node can accept more sends after
    ///     back-pressure.
    /// </summary>
    void SetSendReadyHandler(SpotSendReadyHandler handler);

    /// <summary>
    ///     Gets or sets the dispatch workers min.
    /// </summary>
    int DispatchWorkersMin { get; set; }

    /// <summary>
    ///     Gets or sets the dispatch workers max.
    /// </summary>
    int DispatchWorkersMax { get; set; }

    /// <summary>
    ///     Gets the routing id.
    /// </summary>
    RoutingId RoutingId { get; }

    /// <summary>
    ///     Gets the last endpoint.
    /// </summary>
    string LastEndpoint { get; }

    /// <summary>
    ///     Sets the routing id.
    /// </summary>
    void SetRoutingId(RoutingId routingId);

    /// <summary>
    ///     Sets the local SPOT publisher socket routing id.
    /// </summary>
    void SetPublisherRoutingId(RoutingId routingId);

    /// <summary>
    ///     Sets the local SPOT subscriber socket routing id.
    /// </summary>
    void SetSubscriberRoutingId(RoutingId routingId);

    /// <summary>
    ///     Sets the router bind endpoint.
    /// </summary>
    void SetRouterBind(string endpoint);

    /// <summary>
    ///     Sets the publisher bind endpoint.
    /// </summary>
    void SetPubBind(string endpoint);

    /// <summary>
    ///     Configures this node as a TLS server. Apply before binding.
    /// </summary>
    void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false);

    /// <summary>
    ///     Configures this node as a TLS client. Apply before connecting.
    /// </summary>
    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);
}
