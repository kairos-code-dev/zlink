/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.service.spot.RequestOp;
import systems.zlink.contracts.service.spot.SendOp;

public interface DealerSocket extends Socket {
    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);
    void disconnectRid(RoutingId routingId);
    void attachDiscovery(Discovery discovery);
    void setChannelName(String channelName);
    String getChannelName();
    void setRoutingId(RoutingId rid);
    RoutingId routingId();
    SendOp send();
    boolean recv(Received result, RecvFlags flags);
    RequestOp request();
    @Override DealerSocketOptions options();
}
