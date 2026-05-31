/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

/** Builds an actor destroy operation. */
public interface ActorDestroyOperation {
    /** Sets the operation timeout, replacing any previous value. */
    ActorDestroyOperation timeout(Duration timeout);
    /** Submits the operation and asynchronously returns the reply parts. */
    CompletableFuture<List<Message>> submitAsync();
    /** Submits the operation; the result is delivered to {@code callback}. */
    boolean submit(ReplyHandler callback);
}
