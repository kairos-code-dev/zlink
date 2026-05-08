/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import java.util.Objects;

public final class ActorJoinRequest implements AutoCloseable {
    private final ActorJoinInfo info;
    private final Message message;

    ActorJoinRequest(ActorJoinInfo info, Message message) {
        this.info = Objects.requireNonNull(info, "info");
        this.message = Objects.requireNonNull(message, "message");
    }

    public ActorJoinInfo info() {
        return info;
    }

    public Message message() {
        return message;
    }

    @Override
    public void close() {
        message.close();
    }
}
