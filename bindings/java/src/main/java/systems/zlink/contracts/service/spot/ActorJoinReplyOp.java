/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.Message;

/**
 * Reply builder for {@link Spot#replyActorJoin(ActorJoinRequest, boolean)}.
 * Multipart reply payload is optional; a zero-message submit is allowed.
 */
public interface ActorJoinReplyOp {
    ActorJoinReplyOp message(Message part);
    void submit();
}
