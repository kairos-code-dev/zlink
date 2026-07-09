/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
/**
 * Reply builder for {@link Spot#replyActorJoin(ActorJoinRequest, int)}.
 * Multipart reply payload is optional; a zero-message submit is allowed.
 */
public interface ActorJoinReplyOperation
  extends MessageBuilderStage<ActorJoinReplyOperation> {
    ActorJoinReplyOperation message(Message part);
    void submit();
}
