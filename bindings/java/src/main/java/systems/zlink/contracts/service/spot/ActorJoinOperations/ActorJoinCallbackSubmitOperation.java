/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import java.time.Duration;

public interface ActorJoinCallbackSubmitOperation {
    ActorJoinCallbackSubmitOperation message(Message part);
    ActorJoinCallbackSubmitOperation timeout(Duration timeout);
    ActorJoinCallbackSubmitOperation flags(SendFlags flags);
    boolean submit(ActorJoinHandler callback);
}
