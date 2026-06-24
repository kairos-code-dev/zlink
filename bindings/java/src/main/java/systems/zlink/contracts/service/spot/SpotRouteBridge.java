/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.RouterSocket;
import java.util.List;

/** Bridges caller-owned channel sockets to the local SPOT routed plane. */
public interface SpotRouteBridge extends AutoCloseable {
    void attachRouterChannel(String channelName, RouterSocket router);

    void attachRouterChannel(
        String channelName,
        RouterSocket router,
        SpotRouteBridgeEndpointOptions options);

    SendOperation send(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid);

    RequestOperation request(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid);

    boolean handleRouterReceived(
        String channelName,
        RoutingId sourceNodeRid,
        long requestSeq,
        List<Message> parts);

    int drain();

    void close();
}
