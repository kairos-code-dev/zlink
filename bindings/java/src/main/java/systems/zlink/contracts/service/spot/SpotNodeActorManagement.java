/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.RequestResult;
import java.time.Duration;
import java.util.List;

/** Actor lifecycle, actor routing, and actor session-relay operations. */
public interface SpotNodeActorManagement {
    Actor createActor(String actorId);

    Actor createActor(String actorId, Message request);

    Actor createActor(String actorId, List<Message> requestParts);

    ActorRef actorLookup(String actorId);

    /**
     * Async remote actor lookup. The returned builder is staged: callers
     * configure {@code timeout(...)} then submit via {@code submit()} or
     * {@code submit(callback)}.
     */
    ActorLookupOperation remoteActorGetRef(RoutingId targetNodeRid,
                                                    String actorId);

    /** Async destroy. Succeeds only when the Actor is in the Entry Spot. */
    ActorDestroyOperation destroyActor(ActorRef actor);

    /**
     * Async user-Spot join builder. Completion delivers the final ActorRef,
     * joined Spot rid, join epoch, and reply parts. {@code destSpotRid} must
     * be a user Spot; the Entry Spot is not a valid target.
     */
    ActorJoinOperation joinActor(ActorRef actor,
                                          RoutingId destNodeRid,
                                          RoutingId destSpotRid);

    /** Builds a request-bearing join to {@code destNodeRid}'s Entry Spot. */
    ActorJoinEntrySpotOperation joinActorEntrySpot(
      ActorRef actor,
      RoutingId destNodeRid,
      Message request);

    /** Async leave to the same node's Entry Spot. */
    ActorLeaveOperation leaveActor(ActorRef actor,
                                            RoutingId currentSpotRid);

    /**
     * Actor-to-session relay builder. Fire-and-forget reverse send through the
     * Actor's bound STREAM session.
     */
    SendOperation sendActorBoundSession(ActorRef actor);

    /** Builds a send to a resolved Actor ref; completion acknowledges mailbox handoff. */
    SendOperation sendToActor(ActorRef actor);

    /** Builds a request to a resolved Actor ref; completion returns Actor handler reply parts. */
    RequestOperation requestToActor(ActorRef actor);

    /** Replies to a no-bind actor request described by {@code info}. */
    void replyActorNoBind(ActorRecvInfo info,
                          List<Message> parts,
                          RequestResult result);

    /**
     * Session-to-actor forward builder for a STREAM session route owned by
     * another source SpotNode.
     */
    SendOperation forwardActorBoundSession(
      ActorRef actor,
      RoutingId sourceNodeRid,
      RoutingId sourceSessionRid);

    void bindRemoteActorBoundSession(
      ActorRef actor,
      RoutingId sourceNodeRid,
      RoutingId sourceSessionRid);

    void closeActorBoundSession(ActorRef actor, Duration timeout);
}
