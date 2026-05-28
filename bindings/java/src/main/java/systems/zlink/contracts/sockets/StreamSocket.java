/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.ActorBindOp;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ActorUnbindOp;
import systems.zlink.contracts.service.spot.SendOp;
import systems.zlink.contracts.service.spot.SpotNode;
import java.util.List;

public interface StreamSocket extends Socket {
    void bind(String endpoint);
    void unbind(String endpoint);
    void setRoutingId(RoutingId rid);
    RoutingId routingId();
    SendOp send(RoutingId rid);
    boolean recv(Received result, RecvFlags flags);
    void onPacket(StreamPacketHandler handler);
    void attachActorGateway(SpotNode node);
    ActorBindOp bindActor(RoutingId sessionRid, ActorRef actor);
    ActorUnbindOp unbindActor(RoutingId sessionRid, String actorId);
    SendOp sendBoundActor(RoutingId sessionRid, String actorId);
    List<ActorRef> boundActors(RoutingId sessionRid);
    @Override StreamSocketOptions options();
}
