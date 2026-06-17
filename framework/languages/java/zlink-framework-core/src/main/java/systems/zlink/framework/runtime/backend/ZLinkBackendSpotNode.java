package systems.zlink.framework.runtime.backend;

import java.util.List;
import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendSpotNode extends ZLinkBackendObject {
    RoutingId routingId();

    void setRoutingId(RoutingId routingId);

    void setRouterBind(String endpoint);

    void setPubBind(String endpoint);

    void attachDiscovery(ZLinkBackendDiscovery discovery);

    void connectPeer(String endpoint);

    void connectRouterChannelPeer(String channelName, String endpoint);

    void connectRouterChannelPeerRid(String channelName, RoutingId peerRid, String endpoint);

    void attachSpotRouteChannelDiscovery(
        String channelName,
        ZLinkBackendDiscovery discovery);

    void attachChannelDealer(ZLinkBackendDiscovery discovery, ZLinkBackendDealerSocket dealer);

    void attachChannelDealerManual(String channelName, ZLinkBackendDealerSocket dealer);

    ZLinkBackendSpot createSpot();

    ZLinkBackendSpot entrySpot();

    ZLinkBackendActorRef createActor(String actorId);

    ZLinkBackendActorRef actorLookup(String actorId);

    CompletionStage<ZLinkBackendActorJoinResult> joinActor(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        Duration timeout);

    CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        Message request,
        Duration timeout);

    CompletionStage<Void> destroyActor(
        ZLinkBackendActorRef actor,
        Duration timeout);

    boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags);

    void closeActorBoundSession(ZLinkBackendActorRef actor, Duration timeout);

    SpotNodeStatus status();

    List<SpotNodePeerEntry> peers();

    List<SpotNodeSubjectEntry> subjects();
}
