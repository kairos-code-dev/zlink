package systems.zlink.framework.runtime.backend;

import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendSpotNode extends ZLinkBackendObject {
    RoutingId routingId();

    void setRoutingId(RoutingId routingId);

    void setRouterBind(String endpoint);

    void setPubBind(String endpoint);

    void attachDiscovery(ZLinkBackendDiscovery discovery);

    void connectPeer(String endpoint);

    void attachChannelDealer(ZLinkBackendDiscovery discovery, ZLinkBackendDealerSocket dealer);

    void attachChannelDealerManual(String channelName, ZLinkBackendDealerSocket dealer);

    ZLinkBackendSpot createSpot();

    ZLinkBackendSpot entrySpot();

    ZLinkBackendActorRef createActor(String actorId);

    ZLinkBackendActorRef actorLookup(String actorId);

    boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags);

    ZLinkBackendSpotNodeStatus status();

    List<ZLinkBackendSpotNodePeerEntry> peers();

    List<ZLinkBackendSpotNodeSubjectEntry> subjects();
}
