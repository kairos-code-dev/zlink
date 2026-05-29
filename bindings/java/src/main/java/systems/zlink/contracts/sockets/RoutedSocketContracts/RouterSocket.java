/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.service.spot.ReplyOp;
import systems.zlink.contracts.service.spot.RequestOp;
import systems.zlink.contracts.service.spot.SendOp;

public interface RouterSocket extends Socket {
    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);
    void disconnectRid(RoutingId routingId);
    void attachDiscovery(Discovery discovery);
    void setRoutingId(RoutingId rid);
    RoutingId routingId();
    SendOp send(RoutingId rid);
    boolean recv(Received result, RecvFlags flags);
    RequestOp request(RoutingId rid);
    ReplyOp reply(RoutingId rid, long requestSequence);
    SendOp sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    RequestOp requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    ReplyOp replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                        long requestSeq);
    @Override RouterSocketOptions options();
}
