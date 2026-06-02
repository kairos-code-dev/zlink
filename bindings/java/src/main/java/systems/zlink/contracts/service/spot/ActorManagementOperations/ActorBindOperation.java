/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletionStage;

/** Builds an actor-to-session bind operation. */
public interface ActorBindOperation {
    /** Sets the operation timeout, replacing any previous value. */
    ActorBindOperation timeout(Duration timeout);
    /** Submits the operation and asynchronously returns the reply parts. */
    CompletionStage<List<Message>> submitAsync();
    /** Submits the operation; the result is delivered to {@code callback}. */
    boolean submit(ReplyHandler callback);
}
