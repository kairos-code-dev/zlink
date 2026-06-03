/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import java.util.List;
import java.util.Objects;

/** A pending request from an actor to join a spot, awaiting a reply. */
public final class ActorJoinRequest implements AutoCloseable {
    private final ActorJoinInfo info;
    private final List<Message> parts;

    ActorJoinRequest(ActorJoinInfo info, Message message) {
        this(info, List.of(message));
    }

    ActorJoinRequest(ActorJoinInfo info, List<Message> parts) {
        this.info = Objects.requireNonNull(info, "info");
        Objects.requireNonNull(parts, "parts");
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("parts must not be empty");
        }
        this.parts = List.copyOf(parts);
    }

    public ActorJoinInfo info() {
        return info;
    }

    public Message message() {
        return parts.get(0);
    }

    public List<Message> parts() {
        return parts;
    }

    @Override
    public void close() {
        parts.forEach(Message::close);
    }
}
