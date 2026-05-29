// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the discovery contract.
/// </summary>
public interface IDiscovery : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets or sets the route value max size.
    /// </summary>
    int RouteValueMaxSize { get; }

    /// <summary>
    /// Gets or sets the spot owner sync enabled.
    /// </summary>
    bool SpotOwnerSyncEnabled { get; set; }

    /// <summary>
    /// Gets or sets the actor route sync enabled.
    /// </summary>
    bool ActorRouteSyncEnabled { get; set; }

    /// <summary>
    /// Connects to the endpoint.
    /// </summary>
    void ConnectRegistry(string registryPubEndpoint);

    /// <summary>
    /// Sets the tls client.
    /// </summary>
    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);

    /// <summary>
    /// Sets the value.
    /// </summary>
    void SetValue(long value);

    /// <summary>
    /// Gets the value.
    /// </summary>
    long GetValue();

    /// <summary>
    /// Gets or sets the member peers.
    /// </summary>
    MemberPeerEntry[] MemberPeers();

    /// <summary>
    /// Gets or sets the resolve spot.
    /// </summary>
    SpotRoute ResolveSpot(RoutingId spotRid);

    /// <summary>
    /// Gets or sets the resolve actor.
    /// </summary>
    ActorRoute ResolveActor(string actorId);

    /// <summary>
    /// Binds the endpoint.
    /// </summary>
    void BindRoute(uint kind, ReadOnlySpan<byte> key, ReadOnlySpan<byte> value);

    /// <summary>
    /// Unbinds the endpoint.
    /// </summary>
    void UnbindRoute(uint kind, ReadOnlySpan<byte> key);

    /// <summary>
    /// Gets or sets the resolve route.
    /// </summary>
    DiscoveryRoute ResolveRoute(uint kind, ReadOnlySpan<byte> key);

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}

/// <summary>
/// Represents discovery route kind.
/// </summary>
public static class DiscoveryRouteKind
{
    /// <summary>
    /// Gets or sets the invalid.
    /// </summary>
    public const uint Invalid = 0;
    /// <summary>
    /// Gets or sets the actor.
    /// </summary>
    public const uint Actor = 1;
    /// <summary>
    /// Gets or sets the spot name.
    /// </summary>
    public const uint SpotName = 2;
    /// <summary>
    /// Gets or sets the actor session.
    /// </summary>
    public const uint ActorSession = 3;
}

/// <summary>
/// Describes spot route data.
/// </summary>
/// <param name="SpotRid">The spot routing id value.</param>
/// <param name="OwnerNodeRid">The owner node routing id value.</param>
/// <param name="SpotKind">The spot kind value.</param>
public sealed record SpotRoute(RoutingId SpotRid, RoutingId OwnerNodeRid,
    SpotKind SpotKind);

/// <summary>
/// Represents discovery route.
/// </summary>
public sealed class DiscoveryRoute : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Creates a discovery route instance.
    /// </summary>
    public DiscoveryRoute(RoutingId ownerRoutingId, Message value)
    {
        Value = value ?? throw new ArgumentNullException(nameof(value));
        OwnerRoutingId = ownerRoutingId;
    }

    /// <summary>
    /// Gets or sets the owner routing id.
    /// </summary>
    public RoutingId OwnerRoutingId { get; }

    /// <summary>
    /// Gets or sets the value.
    /// </summary>
    public Message Value { get; }

    /// <summary>
    /// Releases resources owned by this instance.
    /// </summary>
    public void Dispose()
    {
        Value.Dispose();
    }

    /// <summary>
    /// Releases resources owned by this instance.
    /// </summary>
    /// <returns>The operation result.</returns>
    public ValueTask DisposeAsync()
    {
        Value.Dispose();
        return ValueTask.CompletedTask;
    }
}
