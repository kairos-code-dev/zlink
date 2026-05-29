/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;

public interface ActorJoinSubmitOperation {
    ActorJoinSubmitOperation message(Message part);
    ActorJoinSubmitOperation timeout(Duration timeout);
    ActorJoinCallbackSubmitOperation flags(SendFlags flags);
    CompletableFuture<ActorJoinCompletion> submitAsync();
    boolean submit(ActorJoinHandler callback);
}
