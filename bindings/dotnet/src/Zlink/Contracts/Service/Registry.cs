// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the registry contract.
/// </summary>
public interface IRegistry : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Binds the endpoint.
    /// </summary>
    void Bind(string pubEndpoint, string routerEndpoint);

    /// <summary>
    /// Sets the id.
    /// </summary>
    void SetId(uint registryId);

    /// <summary>
    /// Sets the option.
    /// </summary>
    void SetOption(RegistryOption option, uint value);

    /// <summary>
    /// Gets the option.
    /// </summary>
    uint GetOption(RegistryOption option);

    /// <summary>
    /// Adds the peer.
    /// </summary>
    void AddPeer(string peerPubEndpoint);

    /// <summary>
    /// Sets the heartbeat.
    /// </summary>
    void SetHeartbeat(TimeSpan interval, TimeSpan timeout);

    /// <summary>
    /// Sets the broadcast interval.
    /// </summary>
    void SetBroadcastInterval(TimeSpan interval);

    /// <summary>
    /// Sets the tls server.
    /// </summary>
    void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false);

    /// <summary>
    /// Sets the tls client.
    /// </summary>
    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);

    /// <summary>
    /// Gets the current status.
    /// </summary>
    RegistryStatus Status();

    /// <summary>
    /// Gets registry service summary entries.
    /// </summary>
    RegistryServiceSummaryEntry[] ServiceSummary(
        RegistryServiceSummaryFilter? filter = null);

    /// <summary>
    /// Converts the value to pology.
    /// </summary>
    RegistryTopologyEntry[] Topology(
        RegistryTopologyFilter? filter = null);

    /// <summary>
    /// Gets registry member peer entries.
    /// </summary>
    MemberPeerEntry[] MemberPeers(string channelName);

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}
