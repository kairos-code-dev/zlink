/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import java.time.Duration;
import java.util.List;

/** A logical destination for direct messaging and logical multicast. */
public interface Spot extends AutoCloseable {
    /** Returns a status snapshot of this spot. */
    SpotStatus status();

    /** Returns this spot's routing id. */
    RoutingId routingId();

    /** Sends a message to a channel from this spot. */
    void sendToChannel(String channel, List<Message> parts, SendFlags flags);

    /** Sends a request to a channel from this spot. */
    OperationId requestToChannel(String channel, List<Message> parts, SendFlags flags,
                                 Duration timeout);

    /** Sends a message to another spot. */
    void sendToSpot(RoutingId targetNodeRid, RoutingId targetSpotRid,
                    long targetSpotGeneration, List<Message> parts, SendFlags flags);

    /** Sends a request to another spot. */
    OperationId requestToSpot(RoutingId targetNodeRid, RoutingId targetSpotRid,
                              long targetSpotGeneration, List<Message> parts,
                              SendFlags flags, Duration timeout);

    /** Publishes a topic message on a channel from this spot. */
    void publish(String channel, String topic, List<Message> parts, SendFlags flags);

    /** Adds a topic subscription on a channel. */
    void setSubscription(String channel, String topicFilter, SubscriptionKind kind);

    /** Removes a topic subscription on a channel. */
    void unsetSubscription(String channel, String topicFilter, SubscriptionKind kind);

    @Override
    void close();
}
