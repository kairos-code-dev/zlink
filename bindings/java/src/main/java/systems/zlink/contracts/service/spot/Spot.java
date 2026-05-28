/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.messaging.SubscriptionEntry;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.SendReadyHandler;
import systems.zlink.contracts.sockets.SpotDispatchEventHandler;
import java.time.Duration;
import java.util.List;
import java.util.Optional;

/**
 * Unified spot service handle aligned to the current core publish/subscribe
 * service model.
 */
public interface Spot extends AutoCloseable {

    public abstract void setRoutingId(RoutingId rid);

    public abstract RoutingId routingId();

    public abstract Duration requestTimeout();

    public abstract void requestTimeout(Duration value);

    public abstract SendOp publish(String topicId);

    public abstract SendOp sendChannel(String channelName);

    public abstract SendOp sendToSpot(RoutingId destNodeRid,
                                      RoutingId destSpotRid);

    public abstract RequestOp requestChannel(String channelName);

    public abstract RequestOp requestToSpot(RoutingId destNodeRid,
                                            RoutingId destSpotRid);

    public abstract RequestOp requestToRouter(RoutingId peerRid);

    public abstract ReplyOp replyToSpot(RoutingId destNodeRid,
                                        RoutingId destSpotRid,
                                        long requestSeq);

    public abstract ReplyOp replyToRouter(RoutingId peerRid, long requestSeq);

    public abstract void setSubscription(String topicId);

    public abstract void unsetSubscription(String topicIdOrPattern);

    public abstract Optional<SubscriptionEntry> subscriptionAt(int index);

    public abstract void onSendReady(SendReadyHandler handler);

    public abstract boolean subscribe(TopicMessage result, RecvFlags flags);

    public abstract boolean receiveSubscriptionEvent(SubscriptionEvent result,
                                                     RecvFlags flags);

    public abstract boolean recvRouted(Received result, RecvFlags flags);

    public abstract void onDispatchEvent(SpotDispatchEventHandler handler);

    public abstract ActorJoinRequest recvActorJoin(RecvFlags flags);

    public abstract ActorJoinRequest recvActorJoin();

    public abstract ActorJoinReplyOp replyActorJoin(ActorJoinRequest request,
                                                    int joinResultCode);

    public abstract SpotActorLifecycleEvent recvActorLifecycle(
      RecvFlags flags);

    public abstract SpotActorLifecycleEvent recvActorLifecycle();

    public abstract List<ActorRef> actors();

    @Override
    public abstract void close();
}
