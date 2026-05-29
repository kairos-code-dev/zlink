/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import java.util.List;

/** Callback for {@link ActorJoinSubmitOperation#submit(ActorJoinHandler)}. */
@FunctionalInterface
public interface ActorJoinHandler {
    void onJoinResult(ActorJoinResult result, List<Message> replyParts);
}
