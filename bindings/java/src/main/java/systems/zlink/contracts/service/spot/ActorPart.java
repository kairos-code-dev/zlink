/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.Message;
import java.util.Objects;

public record ActorPart(ActorRecvInfo info, Message message,
                        boolean hasMore) implements AutoCloseable {
    public ActorPart {
        Objects.requireNonNull(info, "info");
        Objects.requireNonNull(message, "message");
    }

    @Override
    public void close() {
        message.close();
    }
}
