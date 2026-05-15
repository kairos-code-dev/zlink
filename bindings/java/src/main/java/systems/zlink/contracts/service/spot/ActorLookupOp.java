/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import java.time.Duration;
import java.util.concurrent.CompletableFuture;

public interface ActorLookupOp {
    ActorLookupOp timeout(Duration timeout);
    CompletableFuture<ActorLookupResult> submitAsync();
    boolean submit(ActorLookupHandler callback);
}
