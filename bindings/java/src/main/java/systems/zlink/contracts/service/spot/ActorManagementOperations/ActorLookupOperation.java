/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


import java.time.Duration;
import java.util.concurrent.CompletableFuture;

/** Builds a remote actor lookup operation. */
public interface ActorLookupOperation {
    ActorLookupOperation timeout(Duration timeout);
    CompletableFuture<ActorLookupResult> submitAsync();
    boolean submit(ActorLookupHandler callback);
}
