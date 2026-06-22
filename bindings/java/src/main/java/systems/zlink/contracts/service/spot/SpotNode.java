/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.PubSocket;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/** Lifecycle and topology facade for the current unified spot node model. */
public interface SpotNode extends AutoCloseable {
    /**
     * Result of atomically getting or creating a local logical spot.
     * @param spot the spot that was retrieved or created
     * @param created true if the spot was newly created; false if it already existed
     */
    public record SpotGetOrCreateResult(Spot spot, boolean created) {}

    void setPubBind(String endpoint);

    void setRouterBind(String endpoint);

    void connectPeer(String peerEndpoint);

    void connectPeerRid(RoutingId targetNodeRid, String peerEndpoint);

    void disconnectPeer(String peerEndpoint);

    void disconnectPeerRid(RoutingId targetNodeRid);

    void attachDiscovery(Discovery discovery);

    SpotRouteBridge createRouteBridge();

    SpotNodePublisher createPublisher();

    void setTlsServer(String certPem, String keyPem,
                                      boolean requireClientCert);

    void setTlsClient(String caCertPem, String hostname,
                                      boolean trustSystem);

    void setRoutingId(RoutingId rid);

    RoutingId getRoutingId();

    Spot createSpot();

    Spot entrySpot();

    Optional<Spot> spotLookup(RoutingId spotRid);

    SpotGetOrCreateResult getOrCreateSpot(RoutingId spotRid);

    Actor createActor(String actorId);

    ActorRef actorLookup(String actorId);

    /** Builds an unchecked remote Actor ref for request APIs. */
    static ActorRef remoteActorRef(RoutingId targetNodeRid,
                                   String actorId) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(actorId, "actorId");
        return new ActorRef(targetNodeRid, actorId, 0L);
    }

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

    /**
     * Session-to-actor forward builder for a STREAM session route owned by
     * another source SpotNode.
     */
    SendOperation forwardActorBoundSession(
      ActorRef actor,
      RoutingId sourceNodeRid,
      RoutingId sourceSessionRid);

    void closeActorBoundSession(ActorRef actor, Duration timeout);

    AutoHwmProfile routerHwmProfile();

    void routerHwmProfile(AutoHwmProfile profile);

    int routerHighWaterMark();

    void routerHighWaterMark(int value);

    AutoHwmProfile pubSubHwmProfile();

    void pubSubHwmProfile(AutoHwmProfile profile);

    int pubSubHighWaterMark();

    void pubSubHighWaterMark(int value);

    int dispatchWorkersMin();

    void dispatchWorkersMin(int value);

    int dispatchWorkersMax();

    void dispatchWorkersMax(int value);

    /** Returns the current node status snapshot. */
    SpotNodeStatus status();

    /** Returns the current peer snapshot. */
    List<SpotNodePeerEntry> peers();

    /** Returns peer entries matching the supplied filter. */
    List<SpotNodePeerEntry> peersQuery(
      SpotNodePeerFilter filter);

    /** Returns the current subject snapshot. */
    List<SpotNodeSubjectEntry> subjects();

    /** Returns subject entries matching the supplied filter. */
    List<SpotNodeSubjectEntry> subjects(
      SpotNodeSubjectFilter filter);

    /** Returns diagnostic socket snapshot rows that exist on this node. */
    List<SpotNodeSocketEntry> internalSockets();

    List<SpotNodeSpotEntry> spots();

    List<SpotNodeActorEntry> actors();

    /** Returns diagnostic socket snapshot rows matching the supplied filter. */
    List<SpotNodeSocketEntry> internalSockets(
      SpotNodeSocketFilter filter);

    @Override
    void close();
}
