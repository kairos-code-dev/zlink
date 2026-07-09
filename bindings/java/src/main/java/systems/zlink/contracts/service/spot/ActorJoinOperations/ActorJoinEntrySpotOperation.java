/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

/** Builds an actor join to an entry spot. */
public interface ActorJoinEntrySpotOperation
  extends MessageBuilderStage<ActorJoinEntrySpotOperation>,
          TimeoutSubmitOperation<ActorJoinEntrySpotCompletion,
              ActorJoinEntrySpotHandler> {
    ActorJoinEntrySpotOperation message(Message part);
    ActorJoinEntrySpotOperation timeout(Duration timeout);
    ActorJoinEntrySpotOperation flags(SendFlags flags);
    CompletionStage<ActorJoinEntrySpotCompletion> submit();
    boolean submit(ActorJoinEntrySpotHandler callback);
}
