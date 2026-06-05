/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.messaging.SubscriptionEntry;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.SendReadyHandler;
import java.time.Duration;
import java.util.List;
import java.util.Optional;

/**
 * Unified spot service handle aligned to the current core publish/subscribe
 * service model.
 */
public interface Spot extends AutoCloseable {

    void setRoutingId(RoutingId rid);

    RoutingId getRoutingId();

    Duration requestTimeout();

    void requestTimeout(Duration value);

    SendOperation publish(String topicId);

    SendOperation sendToChannel(String channelName);

    SendOperation sendToSpot(RoutingId destNodeRid,
                                      RoutingId destSpotRid);

    RequestOperation requestToChannel(String channelName);

    RequestOperation requestToSpot(RoutingId destNodeRid,
                                            RoutingId destSpotRid);

    RequestOperation requestToRouter(RoutingId peerRid);

    ReplyOperation replyToSpot(RoutingId destNodeRid,
                                        RoutingId destSpotRid,
                                        long requestSeq);

    ReplyOperation replyToRouter(RoutingId peerRid, long requestSeq);

    void setSubscription(String topicId);

    void unsetSubscription(String topicIdOrPattern);

    Optional<SubscriptionEntry> subscriptionAt(int index);

    void setSendReadyHandler(SendReadyHandler handler);

    boolean subscribe(TopicMessage result, RecvFlags flags);

    boolean receiveSubscriptionEvent(SubscriptionEvent result,
                                                     RecvFlags flags);

    boolean recvRouted(Received result, RecvFlags flags);

    void setDispatchHandler(SpotDispatchEventHandler handler);

    ActorJoinRequest recvActorJoin(RecvFlags flags);

    ActorJoinRequest recvActorJoin();

    ActorJoinReplyOperation replyActorJoin(ActorJoinRequest request,
                                                    int joinResultCode);

    SpotActorLifecycleEvent recvActorLifecycle(
      RecvFlags flags);

    SpotActorLifecycleEvent recvActorLifecycle();

    List<ActorRef> actors();

    @Override
    void close();
}
