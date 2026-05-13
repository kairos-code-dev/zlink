/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import java.util.List;

public record ActorJoinCompletion(ActorJoinResult result,
                                  List<Message> replyParts) {
}
