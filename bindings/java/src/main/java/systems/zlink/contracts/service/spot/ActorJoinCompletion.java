/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.Message;
import java.util.List;

public record ActorJoinCompletion(ActorJoinResult result,
                                  List<Message> replyParts) {
}
