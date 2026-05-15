/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;
import java.util.List;

/** Callback for {@link ActorJoinSubmitOp#submit(ActorJoinHandler)}. */
@FunctionalInterface
public interface ActorJoinHandler {
    void onJoinResult(ActorJoinResult result, List<Message> replyParts);
}
