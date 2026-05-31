/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.ActorBindOperation;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ActorUnbindOperation;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.SpotNode;
import java.util.List;

/** Exchanges framed packets with raw TCP peers and can host actor gateways. */
public interface StreamSocket extends Socket {
    void bind(String endpoint);
    void unbind(String endpoint);
    void setRoutingId(RoutingId rid);
    RoutingId getRoutingId();
    SendOperation send(RoutingId rid);
    boolean recv(Received result, RecvFlags flags);
    void onPacket(StreamPacketHandler handler);
    void attachActorGateway(SpotNode node);
    ActorBindOperation bindActor(RoutingId sessionRid, ActorRef actor);
    ActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId);
    SendOperation sendBoundActor(RoutingId sessionRid, String actorId);
    List<ActorRef> boundActors(RoutingId sessionRid);
    @Override StreamSocketOptions options();
}
