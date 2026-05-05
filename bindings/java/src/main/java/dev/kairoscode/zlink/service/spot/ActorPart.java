/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;
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
