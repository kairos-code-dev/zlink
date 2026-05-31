/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import java.util.Objects;

/**
 * A message received for an actor, with its routing metadata.
 * @param info the actor receive metadata
 * @param message the message payload; caller must close it
 * @param hasMore whether more parts follow in the same message
 */
public record ActorReceived(ActorRecvInfo info, Message message,
                        boolean hasMore) implements AutoCloseable {
    public ActorReceived {
        Objects.requireNonNull(info, "info");
        Objects.requireNonNull(message, "message");
    }

    @Override
    public void close() {
        message.close();
    }
}
