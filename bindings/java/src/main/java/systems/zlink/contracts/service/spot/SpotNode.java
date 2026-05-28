/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.PubSocket;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/** Lifecycle and topology facade for the current unified spot node model. */
public interface SpotNode extends AutoCloseable {
    /** Result of atomically getting or creating a local logical spot. */
    public record SpotGetOrCreateResult(Spot spot, boolean created) {}

    public abstract void setPubBind(String endpoint);

    public abstract void setRouterBind(String endpoint);

    public abstract void connectPeer(String peerEndpoint);

    public abstract void disconnectPeer(String peerEndpoint);

    public abstract void disconnectPeerRid(RoutingId targetNodeRid);

    public abstract void connectRouterChannelPeer(String channelName,
                                                  String endpoint);

    public abstract void disconnectRouterChannelPeer(String channelName,
                                                     String endpoint);

    public abstract void disconnectRouterChannelPeerRid(String channelName,
                                                        RoutingId peerRid);

    public abstract void attachDiscovery(Discovery discovery);

    public abstract void attachSpotRouteChannelDiscovery(String channelName,
                                                         Discovery discovery);

    public abstract void attachChannelDealer(Discovery discovery,
                                             DealerSocket dealer);

    public abstract void attachChannelDealerManual(String channelName,
                                                   DealerSocket dealer);

    public abstract void attachPubIngress(PubSocket pub);

    public abstract void setTlsServer(String certPem, String keyPem,
                                      boolean requireClientCert);

    public abstract void setTlsClient(String caCertPem, String hostname,
                                      boolean trustSystem);

    public abstract void setRoutingId(RoutingId rid);

    public abstract RoutingId routingId();

    public abstract Spot createSpot();

    public abstract Spot entrySpot();

    public abstract Optional<Spot> spotLookup(RoutingId spotRid);

    public abstract SpotGetOrCreateResult getOrCreateSpot(RoutingId spotRid);

    public abstract Actor createActor(String actorId);

    public abstract ActorRef actorLookup(String actorId);

    /** Builds an unchecked remote Actor ref for request APIs. */
    public static ActorRef remoteActorRef(RoutingId targetNodeRid,
                                          String actorId) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(actorId, "actorId");
        return new ActorRef(targetNodeRid, actorId, 0L);
    }

    /**
     * Async remote actor lookup. The returned builder is staged: callers
     * configure {@code timeout(...)} then submit via {@code submitAsync()} or
     * {@code submit(callback)}.
     */
    public abstract ActorLookupOp remoteActorGetRef(RoutingId targetNodeRid,
                                                    String actorId);

    /** Async destroy. Succeeds only when the Actor is in the Entry Spot. */
    public abstract ActorDestroyOp destroyActor(ActorRef actor);

    /**
     * Async user-Spot join builder. Completion delivers the final ActorRef,
     * joined Spot rid, join epoch, and reply parts. {@code destSpotRid} must
     * be a user Spot; the Entry Spot is not a valid target.
     */
    public abstract ActorJoinOp joinActor(ActorRef actor,
                                          RoutingId destNodeRid,
                                          RoutingId destSpotRid);

    /**
     * Message-less Entry Spot join builder. Completion delivers the final
     * ActorRef after the Actor is in {@code destNodeRid}'s Entry Spot.
     */
    public abstract ActorJoinEntrySpotOp joinActorEntrySpot(
      ActorRef actor,
      RoutingId destNodeRid);

    /** Async leave to the same node's Entry Spot. */
    public abstract ActorLeaveOp leaveActor(ActorRef actor,
                                            RoutingId currentSpotRid);

    /**
     * Actor-to-session relay builder. Fire-and-forget reverse send through the
     * Actor's bound STREAM session.
     */
    public abstract SendOp sendBoundSessionMsg(ActorRef actor);

    public abstract AutoHwmProfile routerHwmProfile();

    public abstract void routerHwmProfile(AutoHwmProfile profile);

    public abstract int routerHwm();

    public abstract void routerHwm(int value);

    public abstract AutoHwmProfile pubsubHwmProfile();

    public abstract void pubsubHwmProfile(AutoHwmProfile profile);

    public abstract int pubsubHwm();

    public abstract void pubsubHwm(int value);

    public abstract int dispatchWorkersMin();

    public abstract void dispatchWorkersMin(int value);

    public abstract int dispatchWorkersMax();

    public abstract void dispatchWorkersMax(int value);

    /** Returns the current node status snapshot. */
    public abstract SpotNodeStatus status();

    /** Returns the current peer snapshot. */
    public abstract List<SpotNodePeerEntry> peers();

    /** Returns peer entries matching the supplied filter. */
    public abstract List<SpotNodePeerEntry> peersQuery(
      SpotNodePeerFilter filter);

    /** Returns the current subject snapshot. */
    public abstract List<SpotNodeSubjectEntry> subjects();

    /** Returns subject entries matching the supplied filter. */
    public abstract List<SpotNodeSubjectEntry> subjects(
      SpotNodeSubjectFilter filter);

    /** Returns diagnostic socket snapshot rows that exist on this node. */
    public abstract List<SpotNodeSocketEntry> internalSockets();

    public abstract List<SpotNodeSpotEntry> spots();

    public abstract List<SpotNodeActorEntry> actors();

    /** Returns diagnostic socket snapshot rows matching the supplied filter. */
    public abstract List<SpotNodeSocketEntry> internalSockets(
      SpotNodeSocketFilter filter);

    @Override
    public abstract void close();
}
