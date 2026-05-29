/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.service.spot.SendOp;

public interface XPubSocket extends Socket {
    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);
    void disconnectRid(RoutingId routingId);
    SendOp publish(String topicId);
    boolean receiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags);
    @Override PubSocketOptions options();
}
