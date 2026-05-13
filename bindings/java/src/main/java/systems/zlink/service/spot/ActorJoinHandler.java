/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import java.util.List;

/** Callback for {@link ActorJoinSubmitOp#submit(ActorJoinHandler)}. */
@FunctionalInterface
public interface ActorJoinHandler {
    void onJoinResult(ActorJoinResult result, List<Message> replyParts);
}
