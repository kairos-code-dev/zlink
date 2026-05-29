/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.service.spot.SendOperation;

public interface PubSocket extends Socket {
    void bind(String endpoint);
    void connect(String endpoint);
    void unbind(String endpoint);
    void disconnect(String endpoint);
    void disconnectRid(RoutingId routingId);
    void attachDiscovery(Discovery discovery);
    SendOperation publish(String topicId);
    @Override PubSocketOptions options();
}
