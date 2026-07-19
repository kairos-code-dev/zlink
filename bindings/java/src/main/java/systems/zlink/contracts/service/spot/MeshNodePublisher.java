/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import java.util.List;

/** A node-level publisher for topic messages. */
public interface MeshNodePublisher extends AutoCloseable {
    /** Publishes a topic message on a channel. */
    void publish(String channel, String topic, List<Message> parts, SendFlags flags);

    /** Publishes a topic message and returns fan-out accounting detail. */
    PublishDetail publishDetailed(String channel, String topic, List<Message> parts,
                                  SendFlags flags);

    @Override
    void close();
}
