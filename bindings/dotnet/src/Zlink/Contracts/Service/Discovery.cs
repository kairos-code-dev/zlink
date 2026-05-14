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

    void Close();
}
