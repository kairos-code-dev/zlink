/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;

public interface ActorJoinEntrySpotOperation {
    ActorJoinEntrySpotOperation timeout(Duration timeout);
    CompletableFuture<ActorJoinEntrySpotCompletion> submitAsync();
    boolean submit(ActorJoinEntrySpotHandler callback);
}
