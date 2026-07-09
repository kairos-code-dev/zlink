/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletionStage;

/** Builds an actor-from-session unbind operation. */
public interface ActorUnbindOperation
  extends TimeoutSubmitOperation<List<Message>, ReplyHandler> {
    ActorUnbindOperation timeout(Duration timeout);
    CompletionStage<List<Message>> submit();
    boolean submit(ReplyHandler callback);
}
