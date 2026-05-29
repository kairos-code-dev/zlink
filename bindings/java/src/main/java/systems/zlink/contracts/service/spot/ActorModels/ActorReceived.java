/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import java.util.Objects;

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
