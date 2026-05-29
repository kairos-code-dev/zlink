/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.discovery;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.registry.MemberPeerEntry;
import systems.zlink.contracts.service.spot.ActorRoute;

import java.util.List;

/**
 * Fixed-channel discovery view contract.
 *
 * <p>One instance tracks exactly one auto-connect type and channel name, then
 * exposes spot owner resolution plus member peer snapshots for that view.
 */
public interface Discovery extends AutoCloseable {
    int routeValueMaxSize();

    void connectRegistry(String registryEndpoint);

    SpotRoute resolveSpot(RoutingId spotRid);

    ActorRoute resolveActor(String actorId);

    void bindRoute(int kind, byte[] key, byte[] value);

    void unbindRoute(int kind, byte[] key);

    DiscoveryRoute resolveRoute(int kind, byte[] key);

    void setValue(long value);

    long getValue();

    void setSpotOwnerSyncEnabled(boolean enabled);

    boolean isSpotOwnerSyncEnabled();

    void setActorRouteSyncEnabled(boolean enabled);

    boolean isActorRouteSyncEnabled();

    void setTlsClient(String caCertPem, String hostname,
                                      boolean trustSystem);

    List<MemberPeerEntry> memberPeers();

    @Override
    void close();
}
