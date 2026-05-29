/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import java.time.Duration;
import java.util.List;

/**
 * Registry service contract aligned to the current core bind/snapshot/query
 * model.
 */
public interface Registry extends AutoCloseable {
    int OPTION_ID = 0x3801;
    int OPTION_HEARTBEAT_INTERVAL_MS = 0x3802;
    int OPTION_HEARTBEAT_TIMEOUT_MS = 0x3803;
    int OPTION_BROADCAST_INTERVAL_MS = 0x3804;

    void bind(String pubEndpoint, String routerEndpoint);

    void setId(int id);

    void setOption(int option, int value);

    int getOption(int option);

    void addPeer(String peerPubEndpoint);

    void setHeartbeat(Duration interval, Duration timeout);

    void setBroadcastInterval(Duration interval);

    void setTlsServer(String certPem, String keyPem,
                      boolean requireClientCert);

    void setTlsClient(String caCertPem, String hostname,
                      boolean trustSystem);

    RegistryStatus status();

    List<RegistryServiceSummaryEntry> serviceSummary();

    List<RegistryServiceSummaryEntry> serviceSummary(
      RegistryServiceSummaryFilter filter);

    List<MemberPeerEntry> memberPeers(String channelName);

    List<RegistryTopologyEntry> topology();

    List<RegistryTopologyEntry> topology(
      RegistryTopologyFilter filter);

    @Override
    void close();
}
