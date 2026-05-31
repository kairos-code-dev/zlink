/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;

/** Builds an actor join to an entry spot. */
public interface ActorJoinEntrySpotOperation {
    ActorJoinEntrySpotOperation timeout(Duration timeout);
    CompletableFuture<ActorJoinEntrySpotCompletion> submitAsync();
    boolean submit(ActorJoinEntrySpotHandler callback);
}
