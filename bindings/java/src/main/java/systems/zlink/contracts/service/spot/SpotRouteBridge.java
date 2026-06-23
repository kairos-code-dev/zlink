/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.RouterSocket;
import java.util.List;

/** Bridges caller-owned channel sockets to the local SPOT routed plane. */
public interface SpotRouteBridge extends AutoCloseable {
    void attachDealerChannel(String channelName, DealerSocket dealer);

    void attachDealerChannel(
        String channelName,
        DealerSocket dealer,
        SpotRouteBridgeEndpointOptions options);

    void attachRouterChannel(String channelName, RouterSocket router);

    void attachRouterChannel(
        String channelName,
        RouterSocket router,
        SpotRouteBridgeEndpointOptions options);

    void setTargetNode(String channelName, RoutingId targetNodeRid);

    SendOperation send(String channelName, RoutingId targetSpotRid);

    RequestOperation request(String channelName, RoutingId targetSpotRid);

    boolean handleRouterReceived(
        String channelName,
        RoutingId sourceNodeRid,
        List<Message> parts);

    boolean handleRouterReceived(
        String channelName,
        RoutingId sourceNodeRid,
        long requestSeq,
        List<Message> parts);

    int drain();

    void close();
}
