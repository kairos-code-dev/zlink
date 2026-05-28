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

    public abstract void connectRegistry(String registryEndpoint);

    public abstract SpotRoute resolveSpot(RoutingId spotRid);

    public abstract ActorRoute resolveActor(String actorId);

    public abstract void setValue(long value);

    public abstract long getValue();

    public abstract void setSpotOwnerSyncEnabled(boolean enabled);

    public abstract boolean isSpotOwnerSyncEnabled();

    public abstract void setActorRouteSyncEnabled(boolean enabled);

    public abstract boolean isActorRouteSyncEnabled();

    public abstract void setTlsClient(String caCertPem, String hostname,
                                      boolean trustSystem);

    public abstract List<MemberPeerEntry> memberPeers();

    @Override
    public abstract void close();
}
