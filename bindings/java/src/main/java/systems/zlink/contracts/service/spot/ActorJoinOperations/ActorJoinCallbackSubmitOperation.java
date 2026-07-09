/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import java.time.Duration;

/** Callback-submission stage of an actor join builder. */
public interface ActorJoinCallbackSubmitOperation
  extends MessageBuilderStage<ActorJoinCallbackSubmitOperation> {
    /** Adds another join part; consumed on a successful submit. */
    ActorJoinCallbackSubmitOperation message(Message part);
    /** Sets the join timeout, replacing any previous value. */
    ActorJoinCallbackSubmitOperation timeout(Duration timeout);
    /** Sets the send flags, replacing any previous flags. */
    ActorJoinCallbackSubmitOperation flags(SendFlags flags);
    boolean submit(ActorJoinHandler callback);
}
