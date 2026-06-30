// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     A discovery service that learns peer routes from a registry and resolves
///     spots, actors, and custom routes.
/// </summary>
public interface IDiscovery : IDisposable, IAsyncDisposable
{
    /// <summary>
    ///     Gets the maximum size, in bytes, of a route value.
    /// </summary>
    int RouteValueMaxSize { get; }

    /// <summary>
    ///     Gets or sets whether spot ownership changes are synchronized across
    ///     peers.
    /// </summary>
    bool SpotOwnerSyncEnabled { get; set; }

    /// <summary>
    ///     Gets or sets whether actor routes are synchronized across peers.
    /// </summary>
    bool ActorRouteSyncEnabled { get; set; }

    /// <summary>
    ///     Connects to a registry's publish endpoint to receive route updates.
    /// </summary>
    void ConnectRegistry(string registryPubEndpoint);

    /// <summary>
    ///     Configures TLS for the registry connection; apply before
    ///     <see cref="ConnectRegistry" />.
    /// </summary>
    /// <param name="caCertPath">Path to the CA bundle (PEM) used to verify the registry.</param>
    /// <param name="hostname">Expected registry hostname to verify against.</param>
    /// <param name="trustSystem">When true, also trust the host system's CA store.</param>
    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);

    /// <summary>
    ///     Sets the application-defined value advertised for this peer.
    /// </summary>
    void SetValue(long value);

    /// <summary>
    ///     Gets the application-defined value advertised for this peer.
    /// </summary>
    long GetValue();

    /// <summary>
    ///     Returns the registry member peers currently known to this service. The
    ///     caller owns the returned array.
    /// </summary>
    MemberPeerEntry[] MemberPeers();

    /// <summary>
    ///     Resolves the route to the spot identified by <paramref name="spotRid" />.
    /// </summary>
    SpotRoute ResolveSpot(RoutingId spotRid);

    /// <summary>
    ///     Resolves the route to the actor identified by
    ///     <paramref name="actorId" />.
    /// </summary>
    ActorRoute ResolveActor(string actorId);

    /// <summary>
    ///     Binds a custom route, associating <paramref name="value" /> with
    ///     <paramref name="key" /> under route <paramref name="kind" /> (see
    ///     <see cref="DiscoveryRouteKind" />).
    /// </summary>
    void BindRoute(uint kind, ReadOnlySpan<byte> key, ReadOnlySpan<byte> value);

    /// <summary>
    ///     Removes the route bound to <paramref name="key" /> under route
    ///     <paramref name="kind" />.
    /// </summary>
    void UnbindRoute(uint kind, ReadOnlySpan<byte> key);

    /// <summary>
    ///     Resolves the route value bound to <paramref name="key" /> under route
    ///     <paramref name="kind" />.
    /// </summary>
    DiscoveryRoute ResolveRoute(uint kind, ReadOnlySpan<byte> key);

    /// <summary>
    ///     Closes the discovery service and releases its resources.
    /// </summary>
    void Close();
}

/// <summary>
///     The route-kind constants used with <see cref="IDiscovery.BindRoute" /> and
///     related route APIs.
/// </summary>
public static class DiscoveryRouteKind
{
    /// <summary>
    ///     No route kind (unset).
    /// </summary>
    public const uint Invalid = 0;

    /// <summary>
    ///     A route keyed by actor id.
    /// </summary>
    public const uint Actor = 1;

    /// <summary>
    ///     A route keyed by spot name.
    /// </summary>
    public const uint SpotName = 2;

    /// <summary>
    ///     A route keyed by actor session.
    /// </summary>
    public const uint ActorSession = 3;
}

/// <summary>
///     The resolved route to a spot: the spot, its owning node, and its kind.
/// </summary>
/// <param name="SpotRid">The spot's routing id.</param>
/// <param name="OwnerNodeRid">The routing id of the node that owns the spot.</param>
/// <param name="SpotKind">The kind of spot.</param>
public sealed record SpotRoute(
    RoutingId SpotRid,
    RoutingId OwnerNodeRid,
    SpotKind SpotKind);

/// <summary>
///     A resolved custom route: its owner and the bound value payload.
/// </summary>
public sealed class DiscoveryRoute : IDisposable, IAsyncDisposable
{
    /// <summary>
    ///     Creates a discovery route that owns <paramref name="value" />.
    /// </summary>
    public DiscoveryRoute(RoutingId ownerRoutingId, Message value)
    {
        Value = value ?? throw new ArgumentNullException(nameof(value));
        OwnerRoutingId = ownerRoutingId;
    }

    /// <summary>
    ///     Gets the routing id of the peer that owns this route.
    /// </summary>
    public RoutingId OwnerRoutingId { get; }

    /// <summary>
    ///     Gets the bound value payload, owned by this route and disposed with it.
    /// </summary>
    public Message Value { get; }

    /// <summary>
    ///     Releases resources owned by this instance. Disposal is synchronous; this
    ///     returns an already-completed task for callers that await disposal.
    /// </summary>
    public ValueTask DisposeAsync()
    {
        Value.Dispose();
        return ValueTask.CompletedTask;
    }

    /// <summary>
    ///     Releases resources owned by this instance.
    /// </summary>
    public void Dispose()
    {
        Value.Dispose();
    }
}