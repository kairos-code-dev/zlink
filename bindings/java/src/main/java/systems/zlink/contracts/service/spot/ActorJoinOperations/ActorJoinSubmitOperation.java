/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import java.time.Duration;
import java.util.concurrent.CompletionStage;

/** Accepts further parts, timeout, flags, and the terminal submit of an actor join. */
public interface ActorJoinSubmitOperation {
    ActorJoinSubmitOperation message(Message part);
    ActorJoinSubmitOperation timeout(Duration timeout);
    ActorJoinCallbackSubmitOperation flags(SendFlags flags);
    CompletionStage<ActorJoinCompletion> submit();
    boolean submit(ActorJoinHandler callback);
}
