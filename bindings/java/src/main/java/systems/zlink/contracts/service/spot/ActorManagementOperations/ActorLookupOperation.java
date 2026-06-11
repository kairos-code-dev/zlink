/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


import java.time.Duration;
import java.util.concurrent.CompletionStage;

/** Builds a remote actor lookup operation. */
public interface ActorLookupOperation {
    ActorLookupOperation timeout(Duration timeout);
    CompletionStage<ActorLookupResult> submit();
    boolean submit(ActorLookupHandler callback);
}
