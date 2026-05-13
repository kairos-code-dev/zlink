/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import systems.zlink.SendFlags;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;

public interface ActorJoinSubmitOp {
    ActorJoinSubmitOp message(Message part);
    ActorJoinSubmitOp timeout(Duration timeout);
    ActorJoinCallbackSubmitOp flags(SendFlags flags);
    CompletableFuture<ActorJoinCompletion> submitAsync();
    boolean submit(ActorJoinHandler callback);
}
