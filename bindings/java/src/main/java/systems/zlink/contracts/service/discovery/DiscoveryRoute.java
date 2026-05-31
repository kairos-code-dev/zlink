/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.discovery;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import java.util.Objects;

/** A resolved custom route entry: the owning node routing id and a value payload. */
public final class DiscoveryRoute implements AutoCloseable {
    private final RoutingId ownerRoutingId;
    private final Message value;

    public DiscoveryRoute(RoutingId ownerRoutingId, Message value) {
        this.ownerRoutingId = Objects.requireNonNull(ownerRoutingId,
            "ownerRoutingId");
        this.value = Objects.requireNonNull(value, "value");
    }

    public RoutingId ownerRoutingId() {
        return ownerRoutingId;
    }

    public Message value() {
        return value;
    }

    @Override
    public void close() {
        value.close();
    }
}
