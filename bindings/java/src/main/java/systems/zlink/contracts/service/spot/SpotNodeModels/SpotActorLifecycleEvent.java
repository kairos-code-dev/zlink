/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import java.util.List;
import systems.zlink.contracts.messaging.Message;

/**
 * An actor lifecycle event delivered to a lifecycle subscriber.
 * @param kind the kind of lifecycle event
 * @param info details of the lifecycle change
 * @param requestParts request parts supplied when the actor was created
 */
public record SpotActorLifecycleEvent(SpotActorLifecycleEventKind kind,
                                      SpotActorLifecycleInfo info,
                                      List<Message> requestParts)
    implements AutoCloseable {
    public SpotActorLifecycleEvent {
        requestParts = List.copyOf(requestParts);
    }

    public SpotActorLifecycleEvent(SpotActorLifecycleEventKind kind,
                                   SpotActorLifecycleInfo info) {
        this(kind, info, List.of());
    }

    @Override
    public void close() {
        requestParts.forEach(Message::close);
    }
}
