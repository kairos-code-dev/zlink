// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IDiscovery : IDisposable, IAsyncDisposable
{
    int RouteValueMaxSize { get; }

    bool SpotOwnerSyncEnabled { get; set; }

    bool ActorRouteSyncEnabled { get; set; }

    void ConnectRegistry(string registryPubEndpoint);

    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);

    void SetValue(long value);

    long GetValue();

    MemberPeerEntry[] MemberPeers();

    RoutingId ResolveSpot(RoutingId spotRid);

    ActorRoute ResolveActor(string actorId);

    void BindRoute(uint kind, ReadOnlySpan<byte> key, ReadOnlySpan<byte> value);

    void UnbindRoute(uint kind, ReadOnlySpan<byte> key);

    DiscoveryRoute ResolveRoute(uint kind, ReadOnlySpan<byte> key);

    void Close();
}

public static class DiscoveryRouteKind
{
    public const uint Invalid = 0;
    public const uint Actor = 1;
    public const uint SpotName = 2;
    public const uint ActorSession = 3;
}

public sealed class DiscoveryRoute : IDisposable, IAsyncDisposable
{
    public DiscoveryRoute(RoutingId ownerRoutingId, Message value)
    {
        Value = value ?? throw new ArgumentNullException(nameof(value));
        OwnerRoutingId = ownerRoutingId;
    }

    public RoutingId OwnerRoutingId { get; }

    public Message Value { get; }

    public void Dispose()
    {
        Value.Dispose();
    }

    public ValueTask DisposeAsync()
    {
        Value.Dispose();
        return ValueTask.CompletedTask;
    }
}
