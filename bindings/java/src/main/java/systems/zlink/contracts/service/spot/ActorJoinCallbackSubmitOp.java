/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;
import systems.zlink.contracts.SendFlags;
import java.time.Duration;

public interface ActorJoinCallbackSubmitOp {
    ActorJoinCallbackSubmitOp message(Message part);
    ActorJoinCallbackSubmitOp timeout(Duration timeout);
    ActorJoinCallbackSubmitOp flags(SendFlags flags);
    boolean submit(ActorJoinHandler callback);
}
