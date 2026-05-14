// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IRegistry : IDisposable, IAsyncDisposable
{
    void Bind(string pubEndpoint, string routerEndpoint);

    void SetId(uint registryId);

    void SetOption(RegistryOption option, uint value);

    uint GetOption(RegistryOption option);

    void AddPeer(string peerPubEndpoint);

    void SetHeartbeat(TimeSpan interval, TimeSpan timeout);

    void SetBroadcastInterval(TimeSpan interval);

    void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false);

    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);

    RegistryStatus StatusSnapshot();

    RegistryServiceSummaryEntry[] ServiceSummarySnapshot(
        RegistryServiceSummaryFilter? filter = null);

    RegistryTopologyEntry[] TopologySnapshot();

    RegistryTopologyEntry[] TopologyQuery(
        RegistryTopologyFilter? filter = null);

    MemberPeerEntry[] MemberPeers(string channelName);

    void Close();
}
