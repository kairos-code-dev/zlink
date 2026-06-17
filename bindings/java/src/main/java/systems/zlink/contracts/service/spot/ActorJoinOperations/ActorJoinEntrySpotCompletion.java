/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import java.util.List;
import systems.zlink.contracts.messaging.Message;

/**
 * Carries the result of a completed actor entry spot join.
 * @param result the join outcome
 * @param replyParts the reply frames returned by the admitting entry spot
 */
public record ActorJoinEntrySpotCompletion(
        ActorJoinEntrySpotResult result,
        List<Message> replyParts) {
}
