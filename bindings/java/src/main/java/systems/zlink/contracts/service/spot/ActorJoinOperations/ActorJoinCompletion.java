/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import java.util.List;

/**
 * Carries the result and reply parts of a completed actor join.
 * @param result the join outcome
 * @param replyParts the reply message parts; caller owns them on success
 */
public record ActorJoinCompletion(ActorJoinResult result,
                                  List<Message> replyParts) {
}
