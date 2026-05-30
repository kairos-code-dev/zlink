// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// A service registry that members publish to and peers query for topology.
/// </summary>
public interface IRegistry : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Binds the registry's publish and router endpoints so members and peers
    /// can connect.
    /// </summary>
    void Bind(string pubEndpoint, string routerEndpoint);

    /// <summary>
    /// Sets the registry's identifier.
    /// </summary>
    void SetId(uint registryId);

    /// <summary>
    /// Sets a registry option; see <see cref="RegistryOption"/>.
    /// </summary>
    void SetOption(RegistryOption option, uint value);

    /// <summary>
    /// Gets a registry option; see <see cref="RegistryOption"/>.
    /// </summary>
    uint GetOption(RegistryOption option);

    /// <summary>
    /// Adds a peer registry to federate with, by its publish endpoint.
    /// </summary>
    void AddPeer(string peerPubEndpoint);

    /// <summary>
    /// Sets the member heartbeat <paramref name="interval"/> and the
    /// <paramref name="timeout"/> after which a silent member is dropped.
    /// </summary>
    void SetHeartbeat(TimeSpan interval, TimeSpan timeout);

    /// <summary>
    /// Sets how often the registry broadcasts its state.
    /// </summary>
    void SetBroadcastInterval(TimeSpan interval);

    /// <summary>
    /// Configures the registry as a TLS server; apply before
    /// <see cref="Bind"/>.
    /// </summary>
    void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false);

    /// <summary>
    /// Configures TLS for connections to peer registries.
    /// </summary>
    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);

    /// <summary>
    /// Returns a snapshot of the registry's current status.
    /// </summary>
    RegistryStatus Status();

    /// <summary>
    /// Returns a summary of registered services, optionally filtered. The
    /// caller owns the returned array.
    /// </summary>
    RegistryServiceSummaryEntry[] ServiceSummary(
        RegistryServiceSummaryFilter? filter = null);

    /// <summary>
    /// Returns the registry's topology entries, optionally filtered. The caller
    /// owns the returned array.
    /// </summary>
    RegistryTopologyEntry[] Topology(
        RegistryTopologyFilter? filter = null);

    /// <summary>
    /// Returns the member peers registered on <paramref name="channelName"/>.
    /// </summary>
    MemberPeerEntry[] MemberPeers(string channelName);

    /// <summary>
    /// Closes the registry and releases its resources.
    /// </summary>
    void Close();
}
