/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import systems.zlink.SendFlags;
import java.time.Duration;

public interface ActorJoinCallbackSubmitOp {
    ActorJoinCallbackSubmitOp message(Message part);
    ActorJoinCallbackSubmitOp timeout(Duration timeout);
    ActorJoinCallbackSubmitOp flags(SendFlags flags);
    boolean submit(ActorJoinHandler callback);
}
