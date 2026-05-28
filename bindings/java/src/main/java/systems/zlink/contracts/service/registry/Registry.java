/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import java.time.Duration;
import java.util.List;

/**
 * Registry service contract aligned to the current core bind/snapshot/query
 * model.
 */
public interface Registry extends AutoCloseable {
    public static final int OPTION_ID = 0x3801;
    public static final int OPTION_HEARTBEAT_INTERVAL_MS = 0x3802;
    public static final int OPTION_HEARTBEAT_TIMEOUT_MS = 0x3803;
    public static final int OPTION_BROADCAST_INTERVAL_MS = 0x3804;

    public abstract void bind(String pubEndpoint, String routerEndpoint);

    public abstract void setId(int id);

    public abstract void setOption(int option, int value);

    public abstract int getOption(int option);

    public abstract void addPeer(String peerPubEndpoint);

    public abstract void setHeartbeat(Duration interval, Duration timeout);

    public abstract void setBroadcastInterval(Duration interval);

    public abstract void setTlsServer(String certPem, String keyPem,
                                      boolean requireClientCert);

    public abstract void setTlsClient(String caCertPem, String hostname,
                                      boolean trustSystem);

    public abstract RegistryStatus status();

    public abstract List<RegistryServiceSummaryEntry> serviceSummary();

    public abstract List<RegistryServiceSummaryEntry> serviceSummary(
      RegistryServiceSummaryFilter filter);

    public abstract List<MemberPeerEntry> memberPeers(String channelName);

    public abstract List<RegistryTopologyEntry> topology();

    public abstract List<RegistryTopologyEntry> topology(
      RegistryTopologyFilter filter);

    @Override
    public abstract void close();
}
