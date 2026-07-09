/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import java.util.Optional;

/** Topology and local spot operations for a spot node. */
public interface SpotNodeTopology {
    void setPubBind(String endpoint);

    void setRouterBind(String endpoint);

    void connectPeer(String peerEndpoint);

    void connectPeerRid(RoutingId targetNodeRid, String peerEndpoint);

    void disconnectPeer(String peerEndpoint);

    void disconnectPeerRid(RoutingId targetNodeRid);

    SpotRouteBridge createRouteBridge();

    SpotNodePublisher createPublisher();

    void setTlsServer(String certPem, String keyPem,
                                      boolean requireClientCert);

    void setTlsClient(String caCertPem, String hostname,
                                      boolean trustSystem);

    void setRoutingId(RoutingId rid);

    RoutingId getRoutingId();

    void setPublisherRoutingId(RoutingId rid);

    void setSubscriberRoutingId(RoutingId rid);

    Spot createSpot();

    Spot entrySpot();

    Optional<Spot> spotLookup(RoutingId spotRid);

    SpotNode.SpotGetOrCreateResult getOrCreateSpot(RoutingId spotRid);
}
