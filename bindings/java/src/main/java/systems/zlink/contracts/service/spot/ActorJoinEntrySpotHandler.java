/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import java.util.List;
import systems.zlink.contracts.messaging.Message;

/** Callback for {@link ActorJoinEntrySpotOperation#submit(ActorJoinEntrySpotHandler)}. */
@FunctionalInterface
public interface ActorJoinEntrySpotHandler {
    void onJoinEntrySpotResult(ActorJoinEntrySpotResult result,
                               List<Message> replyParts);
}
