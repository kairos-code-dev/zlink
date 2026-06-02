package systems.zlink.framework.runtime.backend;

import java.util.List;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkBackendDiscovery extends ZLinkBackendObject {
    void connectRegistry(String endpoint);

    ZLinkBackendDiscoveryRoute resolveRoute(long kind, byte[] key);

    ZLinkBackendSpotRoute resolveSpot(RoutingId spotRid);

    ZLinkBackendActorRoute resolveActor(String actorId);

    List<ZLinkBackendRegistryTopologyEntry> memberPeers();
}
